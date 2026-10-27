#!/usr/bin/env python3
# Copyright (c) 2026 nanoMODBUS Contributors
# SPDX-License-Identifier: MIT

"""
HIL Validation Script for nanoMODBUS RTU Server (Nucleo-H743ZI)
Communicates via USB-UART adapter (e.g., /dev/ttyUSB0).
"""

import argparse
import struct
import sys
import time
import serial


def calc_crc(data: bytes) -> bytes:
    """Calculate Modbus RTU CRC-16 (little-endian byte order)."""
    crc = 0xFFFF
    for pos in data:
        crc ^= pos
        for _ in range(8):
            if (crc & 1) != 0:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return struct.pack('<H', crc)


class ModbusRtuClient:
    def __init__(self, port: str = "/dev/ttyUSB0", baudrate: int = 115200, timeout: float = 1.0):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.timeout,
            inter_byte_timeout=0.05,
        )

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def transact(self, request_pdu: bytes, desc: str = "", max_retries: int = 1) -> bytes:
        req = request_pdu + calc_crc(request_pdu)
        for attempt in range(max_retries + 1):
            if attempt > 0:
                print(f"  [!] Retrying request ({attempt}/{max_retries})...")
                time.sleep(0.05)
            self.ser.reset_input_buffer()
            print(f"  -> TX: {req.hex(' ')}")
            self.ser.write(req)
            self.ser.flush()

            resp = self.ser.read(256)
            print(f"  <- RX: {resp.hex(' ')}")

            if len(resp) < 4:
                if attempt < max_retries:
                    continue
                raise RuntimeError(f"Response too short ({len(resp)} bytes): {resp.hex(' ')}")

            # Verify CRC
            payload = resp[:-2]
            received_crc = resp[-2:]
            expected_crc = calc_crc(payload)
            if received_crc != expected_crc:
                if attempt < max_retries:
                    continue
                raise ValueError(
                    f"CRC mismatch! Expected {expected_crc.hex(' ')}, got {received_crc.hex(' ')}"
                )

            # Allow inter-frame delay
            time.sleep(0.05)
            return resp


def run_tests(port: str, baudrate: int) -> bool:
    print("=" * 70)
    print("nanoMODBUS RTU Server HIL Validation on Nucleo-H743ZI")
    print(f"Port: {port}, Baud: {baudrate} 8N1")
    print("=" * 70)

    client = ModbusRtuClient(port=port, baudrate=baudrate, timeout=1.0)
    all_passed = True
    test_results = []

    def record_result(name: str, passed: bool, msg: str = ""):
        nonlocal all_passed
        status = "PASS" if passed else "FAIL"
        test_results.append((name, status, msg))
        if not passed:
            all_passed = False
        print(f"[{status}] {name} {('- ' + msg) if msg else ''}\n")

    try:
        # -------------------------------------------------------------
        # Test 1: Read Holding Registers (FC 03)
        # -------------------------------------------------------------
        print("=== Test 1: Read Holding Registers (FC 03) ===")
        print("Reading 4 registers starting at address 0x0000...")
        req1 = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x04])
        resp1 = client.transact(req1, "Test 1: Read Holding Registers")
        unit_id = resp1[0]
        fc = resp1[1]
        byte_count = resp1[2]

        if unit_id == 0x01 and fc == 0x03 and byte_count == 8:
            regs = struct.unpack('>4H', resp1[3:11])
            regs_str = ", ".join(f"0x{r:04X}" for r in regs)
            record_result(
                "Test 1: Read Holding Registers (FC 03)",
                True,
                f"FC=0x03, ByteCount=8, Regs=[{regs_str}]",
            )
        else:
            record_result(
                "Test 1: Read Holding Registers (FC 03)",
                False,
                f"Unexpected header: Unit={unit_id:#x}, FC={fc:#x}, ByteCount={byte_count}",
            )

        # -------------------------------------------------------------
        # Test 2: Write Single Register (FC 06)
        # -------------------------------------------------------------
        print("=== Test 2: Write Single Register (FC 06) ===")
        print("Writing value 0x1234 to holding register at address 0x0002...")
        req2_write = bytes([0x01, 0x06, 0x00, 0x02, 0x12, 0x34])
        resp2_write = client.transact(req2_write, "Test 2: Write Single Register")
        expected_echo = req2_write + calc_crc(req2_write)
        write_ok = (resp2_write == expected_echo)

        print("Reading back holding register at address 0x0002 (FC 03)...")
        req2_read = bytes([0x01, 0x03, 0x00, 0x02, 0x00, 0x01])
        resp2_read = client.transact(req2_read, "Test 2: Readback Single Register")
        val2 = struct.unpack('>H', resp2_read[3:5])[0] if len(resp2_read) >= 7 else None
        read_ok = (resp2_read[1] == 0x03 and resp2_read[2] == 2 and val2 == 0x1234)

        if write_ok and read_ok:
            record_result(
                "Test 2: Write Single Register (FC 06)",
                True,
                f"Echo verified, readback value: 0x{val2:04X}",
            )
        else:
            record_result(
                "Test 2: Write Single Register (FC 06)",
                False,
                f"Write echo match={write_ok}, Readback match={read_ok} (got {val2})",
            )

        # -------------------------------------------------------------
        # Test 3: Write Multiple Registers (FC 16 / 0x10)
        # -------------------------------------------------------------
        print("=== Test 3: Write Multiple Registers (FC 16 / 0x10) ===")
        print("Writing values [0xCAFE, 0xBABE] to address 0x0004...")
        req3_write = bytes([
            0x01,        # Unit ID
            0x10,        # FC 16 (0x10)
            0x00, 0x04,  # Starting address 4
            0x00, 0x02,  # Quantity 2
            0x04,        # Byte count 4
            0xCA, 0xFE,  # Value 1: 0xCAFE
            0xBA, 0xBE,  # Value 2: 0xBABE
        ])
        resp3_write = client.transact(req3_write, "Test 3: Write Multiple Registers")
        expected_resp3 = bytes([0x01, 0x10, 0x00, 0x04, 0x00, 0x02])
        expected_resp3 += calc_crc(expected_resp3)
        write3_ok = (resp3_write == expected_resp3)

        print("Reading back registers starting at address 0x0004 (FC 03)...")
        req3_read = bytes([0x01, 0x03, 0x00, 0x04, 0x00, 0x02])
        resp3_read = client.transact(req3_read, "Test 3: Readback Multiple Registers")
        regs3 = struct.unpack('>2H', resp3_read[3:7]) if len(resp3_read) >= 9 else None
        read3_ok = (resp3_read[1] == 0x03 and resp3_read[2] == 4 and regs3 == (0xCAFE, 0xBABE))

        if write3_ok and read3_ok:
            regs3_str = ", ".join(f"0x{r:04X}" for r in regs3)
            record_result(
                "Test 3: Write Multiple Registers (FC 16 / 0x10)",
                True,
                f"Write response verified, readback values: [{regs3_str}]",
            )
        else:
            record_result(
                "Test 3: Write Multiple Registers (FC 16 / 0x10)",
                False,
                f"Write response match={write3_ok}, Readback match={read3_ok} (got {regs3})",
            )

        # -------------------------------------------------------------
        # Test 4: Write Single Coil (FC 05) & Read Coils (FC 01)
        # -------------------------------------------------------------
        print("=== Test 4: Write Single Coil (FC 05) & Read Coils (FC 01) ===")
        print("Writing coil 3 = ON (0xFF00)...")
        req4_write = bytes([0x01, 0x05, 0x00, 0x03, 0xFF, 0x00])
        resp4_write = client.transact(req4_write, "Test 4: Write Single Coil")
        expected_echo4 = req4_write + calc_crc(req4_write)
        write4_ok = (resp4_write == expected_echo4)

        print("Reading 8 coils starting at address 0x0000 (FC 01)...")
        req4_read = bytes([0x01, 0x01, 0x00, 0x00, 0x00, 0x08])
        resp4_read = client.transact(req4_read, "Test 4: Read Coils")
        coil_byte = resp4_read[3] if len(resp4_read) >= 6 else 0
        bit3_set = bool(coil_byte & (1 << 3))
        read4_ok = (resp4_read[1] == 0x01 and resp4_read[2] == 1 and bit3_set)

        if write4_ok and read4_ok:
            record_result(
                "Test 4: Write Single Coil (FC 05)",
                True,
                f"Echo verified, coil byte=0x{coil_byte:02X}, bit 3 is SET",
            )
        else:
            record_result(
                "Test 4: Write Single Coil (FC 05)",
                False,
                f"Write echo match={write4_ok}, bit 3 set={bit3_set} (byte=0x{coil_byte:02X})",
            )

        # -------------------------------------------------------------
        # Test 5: Exception Response Verification
        # -------------------------------------------------------------
        print("=== Test 5: Exception Response Verification ===")
        print("Sending FC 03 read address 20 (0x14), quantity 4 (out of bounds)...")
        req5 = bytes([0x01, 0x03, 0x00, 0x14, 0x00, 0x04])
        resp5 = client.transact(req5, "Test 5: Exception Response")
        expected_resp5 = bytes([0x01, 0x83, 0x02])
        expected_resp5 += calc_crc(expected_resp5)

        if resp5 == expected_resp5:
            record_result(
                "Test 5: Exception Response Verification",
                True,
                "Received expected Exception 0x02 (ILLEGAL_DATA_ADDRESS)",
            )
        else:
            record_result(
                "Test 5: Exception Response Verification",
                False,
                f"Expected {expected_resp5.hex(' ')}, got {resp5.hex(' ')}",
            )

    finally:
        client.close()

    print("=" * 70)
    print("FINAL SUMMARY:")
    for name, status, msg in test_results:
        print(f"  [{status}] {name}")
    print("=" * 70)
    overall_status = "ALL TESTS PASSED" if all_passed else "SOME TESTS FAILED"
    print(f"Overall Result: {overall_status}")
    print("=" * 70)
    return all_passed


def main():
    parser = argparse.ArgumentParser(description="nanoMODBUS RTU Server HIL Test")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port (default: /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    success = run_tests(port=args.port, baudrate=args.baud)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
