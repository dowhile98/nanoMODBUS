#!/usr/bin/env python3
# Copyright (c) 2026 nanoMODBUS Contributors
# SPDX-License-Identifier: MIT

"""
HIL Validation Script for nanoMODBUS TCP Server (Nucleo-H743ZI)
Communicates via Modbus TCP (Ethernet).
"""

import argparse
import socket
import struct
import sys
import time
from typing import List, Tuple


class ModbusTcpClient:
    def __init__(self, host: str = "192.168.68.100", port: int = 502, timeout: float = 3.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket = None

    def connect(self):
        print(f"Connecting to Modbus TCP server at {self.host}:{self.port} (timeout={self.timeout}s)...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        print("Connected successfully.")

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def _recv_exact(self, num_bytes: int) -> bytes:
        data = bytearray()
        while len(data) < num_bytes:
            chunk = self.sock.recv(num_bytes - len(data))
            if not chunk:
                raise ConnectionError(f"Connection closed by server while waiting for {num_bytes - len(data)} bytes")
            data.extend(chunk)
        return bytes(data)

    def transact(self, request: bytes, desc: str = "") -> bytes:
        """
        Sends request bytes, receives and parses full Modbus TCP response.
        MBAP Header:
          Transaction ID: 2 bytes
          Protocol ID:    2 bytes (0 = Modbus)
          Length:         2 bytes (number of following bytes: unit_id + PDU)
          Unit ID:        1 byte
        """
        if desc:
            print(f"\n--- {desc} ---")
        print(f"  -> TX ({len(request)} B): {request.hex(' ')}")
        self.sock.sendall(request)

        # Receive 6-byte MBAP header prefix (trans_id, proto_id, length)
        mbap_prefix = self._recv_exact(6)
        trans_id, proto_id, remaining_len = struct.unpack(">HHH", mbap_prefix)

        # Receive the remaining bytes specified by the MBAP length field
        rest = self._recv_exact(remaining_len)
        resp = mbap_prefix + rest
        print(f"  <- RX ({len(resp)} B): {resp.hex(' ')}")

        return resp


def run_tests(host: str, port: int, timeout: float) -> bool:
    print("=" * 70)
    print("nanoMODBUS TCP Server HIL Validation on Nucleo-H743ZI")
    print(f"Target: {host}:{port}")
    print("=" * 70)

    client = ModbusTcpClient(host=host, port=port, timeout=timeout)
    all_passed = True
    test_results: List[Tuple[str, str, str]] = []

    def record_result(name: str, passed: bool, msg: str = ""):
        nonlocal all_passed
        status = "PASS" if passed else "FAIL"
        test_results.append((name, status, msg))
        if not passed:
            all_passed = False
        print(f"[{status}] {name} {('- ' + msg) if msg else ''}\n")

    try:
        client.connect()
    except Exception as e:
        print(f"[FAIL] Could not connect to {host}:{port}: {e}")
        return False

    try:
        # =====================================================================
        # Test 1: Read Holding Registers (FC 03)
        # =====================================================================
        try:
            # Request: trans_id=1, proto_id=0, len=6, unit_id=1, fc=3, addr=0, qty=4
            req1 = struct.pack(">HHHBBHH", 1, 0, 6, 1, 3, 0, 4)
            resp1 = client.transact(req1, "Test 1: Read Holding Registers (FC 03, addr=0, qty=4)")

            if len(resp1) < 9:
                record_result("Test 1: Read Holding Registers (FC 03)", False, f"Response too short: {len(resp1)} bytes")
            else:
                trans_id, proto_id, length, unit_id, fc, byte_count = struct.unpack(">HHHBBB", resp1[:9])
                reg_bytes = resp1[9:]
                regs = struct.unpack(f">{len(reg_bytes)//2}H", reg_bytes)

                checks = [
                    (trans_id == 1, f"trans_id expected 1, got {trans_id}"),
                    (proto_id == 0, f"proto_id expected 0, got {proto_id}"),
                    (length == 11, f"MBAP length expected 11, got {length}"),
                    (unit_id == 1, f"unit_id expected 1, got {unit_id}"),
                    (fc == 3, f"fc expected 3, got {fc}"),
                    (byte_count == 8, f"byte_count expected 8, got {byte_count}"),
                    (len(regs) == 4, f"registers count expected 4, got {len(regs)}"),
                ]
                failed_checks = [msg for ok, msg in checks if not ok]
                if failed_checks:
                    record_result("Test 1: Read Holding Registers (FC 03)", False, "; ".join(failed_checks))
                else:
                    regs_hex = [f"0x{r:04X}" for r in regs]
                    record_result("Test 1: Read Holding Registers (FC 03)", True, f"Registers: {regs_hex}")
        except Exception as e:
            record_result("Test 1: Read Holding Registers (FC 03)", False, str(e))

        # =====================================================================
        # Test 2: Write Single Register (FC 06) and Verify
        # =====================================================================
        try:
            # Request: trans_id=2, proto_id=0, len=6, unit_id=1, fc=6, addr=2, val=0x5678
            write_val = 0x5678
            write_addr = 2
            req2_w = struct.pack(">HHHBBHH", 2, 0, 6, 1, 6, write_addr, write_val)
            resp2_w = client.transact(req2_w, f"Test 2: Write Single Register (FC 06, addr={write_addr}, val=0x{write_val:04X})")

            # Verify echo response
            if resp2_w != req2_w:
                record_result(
                    "Test 2: Write Single Register Echo",
                    False,
                    f"Echo mismatch: expected {req2_w.hex(' ')}, got {resp2_w.hex(' ')}",
                )
            else:
                # Read back register at address 2
                req2_r = struct.pack(">HHHBBHH", 3, 0, 6, 1, 3, write_addr, 1)
                resp2_r = client.transact(req2_r, f"Test 2: Read Back Register (FC 03, addr={write_addr}, qty=1)")

                if len(resp2_r) < 11:
                    record_result("Test 2: Read Back Verification", False, f"Response too short: {len(resp2_r)} bytes")
                else:
                    trans_id, proto_id, length, unit_id, fc, byte_count, read_val = struct.unpack(">HHHBBBH", resp2_r[:11])
                    if trans_id != 3 or fc != 3 or byte_count != 2:
                        record_result(
                            "Test 2: Write Single Register (FC 06)",
                            False,
                            f"Header invalid: trans={trans_id}, fc={fc}, byte_count={byte_count}",
                        )
                    elif read_val != write_val:
                        record_result(
                            "Test 2: Write Single Register (FC 06)",
                            False,
                            f"Value mismatch: wrote 0x{write_val:04X}, read 0x{read_val:04X}",
                        )
                    else:
                        record_result(
                            "Test 2: Write Single Register (FC 06)",
                            True,
                            f"Verified addr {write_addr} = 0x{read_val:04X}",
                        )
        except Exception as e:
            record_result("Test 2: Write Single Register (FC 06)", False, str(e))

        # =====================================================================
        # Test 3: Write Multiple Registers (FC 16 / 0x10) and Verify
        # =====================================================================
        try:
            # Request: trans_id=4, proto_id=0, len=11, unit_id=1, fc=0x10, addr=4, qty=2, byte_count=4, vals=[0xDEAD, 0xBEEF]
            write_addr3 = 4
            write_vals3 = [0xDEAD, 0xBEEF]
            req3_w = struct.pack(">HHHBBHHBHH", 4, 0, 11, 1, 0x10, write_addr3, 2, 4, write_vals3[0], write_vals3[1])
            resp3_w = client.transact(
                req3_w,
                f"Test 3: Write Multiple Registers (FC 16, addr={write_addr3}, vals=[0xDEAD, 0xBEEF])",
            )

            # Response for FC 16 is trans_id, proto_id, len=6, unit_id, fc=0x10, addr, qty
            expected_resp3_w = struct.pack(">HHHBBHH", 4, 0, 6, 1, 0x10, write_addr3, 2)
            if resp3_w != expected_resp3_w:
                record_result(
                    "Test 3: Write Multiple Registers Response",
                    False,
                    f"Expected {expected_resp3_w.hex(' ')}, got {resp3_w.hex(' ')}",
                )
            else:
                # Read back registers at address 4
                req3_r = struct.pack(">HHHBBHH", 5, 0, 6, 1, 3, write_addr3, 2)
                resp3_r = client.transact(req3_r, f"Test 3: Read Back Registers (FC 03, addr={write_addr3}, qty=2)")

                if len(resp3_r) < 13:
                    record_result("Test 3: Read Back Verification", False, f"Response too short: {len(resp3_r)} bytes")
                else:
                    trans_id, proto_id, length, unit_id, fc, byte_count, val0, val1 = struct.unpack(">HHHBBBHH", resp3_r[:13])
                    if trans_id != 5 or fc != 3 or byte_count != 4:
                        record_result(
                            "Test 3: Write Multiple Registers (FC 16)",
                            False,
                            f"Header invalid: trans={trans_id}, fc={fc}, byte_count={byte_count}",
                        )
                    elif (val0, val1) != tuple(write_vals3):
                        record_result(
                            "Test 3: Write Multiple Registers (FC 16)",
                            False,
                            f"Values mismatch: expected [0xDEAD, 0xBEEF], read [0x{val0:04X}, 0x{val1:04X}]",
                        )
                    else:
                        record_result(
                            "Test 3: Write Multiple Registers (FC 16)",
                            True,
                            f"Verified addr {write_addr3}..{write_addr3+1} = [0x{val0:04X}, 0x{val1:04X}]",
                        )
        except Exception as e:
            record_result("Test 3: Write Multiple Registers (FC 16)", False, str(e))

        # =====================================================================
        # Test 4: Write Single Coil (FC 05) and Verify
        # =====================================================================
        try:
            # Request: trans_id=6, proto_id=0, len=6, unit_id=1, fc=5, addr=5, val=0xFF00 (ON)
            coil_addr = 5
            req4_w = struct.pack(">HHHBBHH", 6, 0, 6, 1, 5, coil_addr, 0xFF00)
            resp4_w = client.transact(req4_w, f"Test 4: Write Single Coil (FC 05, addr={coil_addr}, ON)")

            if resp4_w != req4_w:
                record_result(
                    "Test 4: Write Single Coil Echo",
                    False,
                    f"Echo mismatch: expected {req4_w.hex(' ')}, got {resp4_w.hex(' ')}",
                )
            else:
                # Read coils: FC 01, addr 0, qty 8
                req4_r = struct.pack(">HHHBBHH", 7, 0, 6, 1, 1, 0, 8)
                resp4_r = client.transact(req4_r, "Test 4: Read Coils (FC 01, addr=0, qty=8)")

                if len(resp4_r) < 10:
                    record_result("Test 4: Read Coils Response", False, f"Response too short: {len(resp4_r)} bytes")
                else:
                    trans_id, proto_id, length, unit_id, fc, byte_count, coils_byte = struct.unpack(">HHHBBBB", resp4_r[:10])
                    bit_5_set = (coils_byte & (1 << coil_addr)) != 0
                    if trans_id != 7 or fc != 1 or byte_count != 1:
                        record_result(
                            "Test 4: Write Single Coil (FC 05)",
                            False,
                            f"Header invalid: trans={trans_id}, fc={fc}, byte_count={byte_count}",
                        )
                    elif not bit_5_set:
                        record_result(
                            "Test 4: Write Single Coil (FC 05)",
                            False,
                            f"Bit {coil_addr} is NOT set in byte 0x{coils_byte:02X} (binary: {bin(coils_byte)})",
                        )
                    else:
                        record_result(
                            "Test 4: Write Single Coil (FC 05)",
                            True,
                            f"Verified coil {coil_addr} is ON (coils byte: 0x{coils_byte:02X})",
                        )
        except Exception as e:
            record_result("Test 4: Write Single Coil (FC 05)", False, str(e))

        # =====================================================================
        # Test 5: Exception Response Verification (FC 03 out of range)
        # =====================================================================
        try:
            # Request: read holding registers addr 25, qty 2 (server has 16 registers)
            req5 = struct.pack(">HHHBBHH", 8, 0, 6, 1, 3, 25, 2)
            resp5 = client.transact(req5, "Test 5: Exception Verification (FC 03, addr=25 [out of bounds], qty=2)")

            if len(resp5) < 9:
                record_result("Test 5: Exception Response Verification", False, f"Response too short: {len(resp5)} bytes")
            else:
                trans_id, proto_id, length, unit_id, fc_err, exc_code = struct.unpack(">HHHBBB", resp5[:9])
                checks5 = [
                    (trans_id == 8, f"trans_id expected 8, got {trans_id}"),
                    (proto_id == 0, f"proto_id expected 0, got {proto_id}"),
                    (length == 3, f"MBAP length expected 3, got {length}"),
                    (unit_id == 1, f"unit_id expected 1, got {unit_id}"),
                    (fc_err == 0x83, f"FC expected 0x83 (error), got 0x{fc_err:02X}"),
                    (exc_code == 0x02, f"Exception code expected 0x02 (ILLEGAL_DATA_ADDRESS), got 0x{exc_code:02X}"),
                ]
                failed_checks5 = [msg for ok, msg in checks5 if not ok]
                if failed_checks5:
                    record_result("Test 5: Exception Response Verification", False, "; ".join(failed_checks5))
                else:
                    record_result(
                        "Test 5: Exception Response Verification",
                        True,
                        f"Received expected exception FC=0x{fc_err:02X}, code=0x{exc_code:02X} (ILLEGAL_DATA_ADDRESS)",
                    )
        except Exception as e:
            record_result("Test 5: Exception Response Verification", False, str(e))

    finally:
        client.close()

    # Summary
    print("=" * 70)
    print("HIL TEST SUMMARY (nanoMODBUS TCP Server):")
    print("=" * 70)
    for name, status, msg in test_results:
        detail = f" - {msg}" if msg else ""
        print(f"[{status:4s}] {name}{detail}")
    print("=" * 70)
    if all_passed:
        print("ALL TESTS PASSED! HIL TCP Server Validation Complete.")
    else:
        print("SOME TESTS FAILED! Check log above.")
    print("=" * 70)

    return all_passed


def main():
    parser = argparse.ArgumentParser(description="nanoMODBUS TCP Server HIL Validation")
    parser.add_argument("--ip", default="192.168.68.100", help="Modbus TCP server IP (default: 192.168.68.100)")
    parser.add_argument("--port", type=int, default=502, help="Modbus TCP port (default: 502)")
    parser.add_argument("--timeout", type=float, default=3.0, help="Socket timeout in seconds (default: 3.0)")

    args = parser.parse_args()
    success = run_tests(host=args.ip, port=args.port, timeout=args.timeout)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
