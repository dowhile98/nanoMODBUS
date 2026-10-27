# Protocol Support & Compliance Matrix

## 1. Transport Protocol Support

| Transport Flavor | nanoMODBUS Core | Zephyr Adapter Layer | CycloneTCP | Status / Compliance Notes |
|---|---|---|---|---|
| **Modbus RTU** | Supported | Supported (`nanomodbus_z_transport_serial.c`) | Not supported (TCP only) | **COMPLIANT** with Modbus Serial Line v1.02 ($t_{1.5}$, $t_{3.5}$, CRC-16, RS-485 DE). |
| **Modbus TCP** | Supported | Supported (`nanomodbus_z_transport_tcp.c`) | Supported (`cyclone_tcp/modbus/`) | **PARTIALLY COMPLIANT**: Validated against Modbus Messaging on TCP/IP v1.0b, but lacks multi-client reactor. |
| **Modbus ASCII** | Not Supported | Not Supported | Not Supported | **NOT IMPLEMENTED**: Obsolete 7-bit LRC protocol. Not recommended for modern industrial products. |
| **Secure Modbus/TCP (TLS)** | Not Supported | Not Supported | Supported (Port 802, TLS 1.2/1.3, RBAC) | **NOT IMPLEMENTED**: Recommended as NICE TO HAVE via Zephyr native TLS sockets. |

---

## 2. Complete Function Code Matrix

| FC | Hex | Function Code Name | nanoMODBUS Client | nanoMODBUS Server | Cyclone Client | Cyclone Server | Spec Status | Remarks |
|:---:|:---:|---|:---:|:---:|:---:|:---:|:---:|---|
| **01** | `0x01` | Read Coils | YES | YES | YES | YES | **COMPLIANT** | Full support, bitfield packing up to 2000 coils |
| **02** | `0x02` | Read Discrete Inputs | YES | YES | YES | YES | **COMPLIANT** | Full support, bitfield packing up to 2000 inputs |
| **03** | `0x03` | Read Holding Registers | YES | YES | YES | YES | **COMPLIANT** | Full support, up to 125 16-bit registers |
| **04** | `0x04` | Read Input Registers | YES | YES | YES | YES | **COMPLIANT** | Full support, up to 125 16-bit registers |
| **05** | `0x05` | Write Single Coil | YES | YES | YES | YES | **COMPLIANT** | Value 0x0000 (OFF) and 0xFF00 (ON) enforced |
| **06** | `0x06` | Write Single Register | YES | YES | YES | YES | **COMPLIANT** | Full support |
| **07** | `0x07` | Read Exception Status | NO | NO | NO | NO | **NOT IMPLEMENTED** | Legacy serial-only function code |
| **08** | `0x08` | Diagnostics | NO | NO | NO | NO | **PARTIALLY IMPLEMENTED** | Wire FC 08 absent; Cyclone has internal diagnostic counters |
| **11** | `0x0B` | Get Comm Event Counter | NO | NO | NO | NO | **NOT IMPLEMENTED** | Optional serial diagnostic code |
| **12** | `0x0C` | Get Comm Event Log | NO | NO | NO | NO | **NOT IMPLEMENTED** | Optional serial diagnostic code |
| **15** | `0x0F` | Write Multiple Coils | YES | YES | YES | YES | **COMPLIANT** | Full support, bitfield up to 1968 coils |
| **16** | `0x10` | Write Multiple Registers | YES | YES | YES | YES | **COMPLIANT** | Full support, up to 123 registers |
| **17** | `0x11` | Report Server ID | NO | NO | NO | NO | **NOT IMPLEMENTED** | Vendor-specific slave identification |
| **20** | `0x14` | Read File Record | YES | YES | NO | NO | **COMPLIANT** (VLA issue) | nanoMODBUS supports it; Cyclone does not! Contains VLA in core. |
| **21** | `0x15` | Write File Record | YES | YES | NO | NO | **COMPLIANT** | nanoMODBUS supports it; Cyclone does not. |
| **22** | `0x16` | Mask Write Register | NO | NO | YES | YES | **GAP** | Cyclone supports FC 22; nanoMODBUS lacks it. Recommended. |
| **23** | `0x17` | Read/Write Multiple Registers | YES | YES | YES | YES | **COMPLIANT** (VLA issue) | Atomic read & write in single transaction. Contains VLA in core. |
| **24** | `0x18` | Read FIFO Queue | NO | NO | NO | NO | **NOT IMPLEMENTED** | Rarely used specialized PLC queue reading |
| **43/14**| `0x2B/0x0E`| Encapsulated Interface (Read Dev ID) | YES | YES | NO | NO | **COMPLIANT** | Basic, Regular, and Extended objects (0x00..0x06, 0x80..0xFF) |

---

## 3. Detailed Standards Compliance Audit

### 3.1 Modbus Application Protocol Specification v1.1b3

#### 1. Maximum PDU Size Limit
- **Requirement**: MODBUS PDU = Function Code (1 byte) + Data (max 252 bytes) = 253 bytes maximum.
- **Evaluation**: **COMPLIANT**. Buffer allocations in `struct nmbs_t` (`uint8_t buf[260]`) accommodate 253-byte PDU + 7-byte MBAP header.

#### 2. Standard Exception Codes & Error Reporting
- **Requirement**: Section 7 defines 9 standard exception codes:
  - 01: `ILLEGAL FUNCTION`
  - 02: `ILLEGAL DATA ADDRESS`
  - 03: `ILLEGAL DATA VALUE`
  - 04: `SERVER DEVICE FAILURE`
  - 05: `ACKNOWLEDGE` (long-duration task accepted)
  - 06: `SERVER DEVICE BUSY` (temporary backpressure)
  - 08: `MEMORY PARITY ERROR`
  - 0A (10): `GATEWAY PATH UNAVAILABLE`
  - 0B (11): `GATEWAY TARGET DEVICE FAILED TO RESPOND`
- **Evaluation**: **NON-COMPLIANT** (Critical Defect in Core).
  - In `nanomodbus.h`:
    ```c
    #define nmbs_error_is_exception(e) ((e) > 0 && (e) < 5)
    ```
  - In `nanomodbus.c` (`recv_res_header`):
    ```c
    if (exception < 1 || exception > 4)
        return NMBS_ERROR_INVALID_RESPONSE;
    ```
  - **Consequence**: If an industrial gateway, PLC, or inverter returns standard exception 05 (Acknowledge) or 06 (Busy), nanoMODBUS **fails to parse the response**, categorizes it as `NMBS_ERROR_INVALID_RESPONSE`, and discards the valid standard exception!
  - In contrast, CycloneTCP (`modbus_common.h`) defines the complete set 01..11.

#### 3. Broadcast Handling
- **Requirement**: Address 0 is reserved for broadcast write requests (FC 05, 06, 15, 16, 21). The server MUST execute the command and return NO response.
- **Evaluation**: **COMPLIANT**. `nanomodbus.c` tracks `nmbs->msg.broadcast` and suppresses transmission of responses.

---

### 3.2 Modbus Messaging on TCP/IP Implementation Guide v1.0b

#### 1. MBAP Length Field Validation & Off-By-One Buffer Overflow
- **Requirement**: Section 4.1 specifies:
  $$\text{Length} = \text{Unit ID (1 byte)} + \text{PDU (max 253 bytes)} \le 254 \text{ bytes}$$
- **nanoMODBUS Implementation Defect**:
  In `nanomodbus.c` lines 371–375:
  ```c
  if (length < 2 || length > 255)
      return NMBS_ERROR_INVALID_TCP_MBAP;

  // Receive the rest of the message
  err = recv(nmbs, length - 2);
  ```
  - `nmbs->msg.buf` is declared as `uint8_t buf[260];` (valid indices 0..259).
  - Prior to line 375, `buf_idx` is advanced to index 8 (having read 2 bytes TID, 2 bytes PID, 2 bytes Length, 1 byte Unit ID, 1 byte FC).
  - If a packet specifies `length = 255` (which passes `length > 255`):
    $$\text{Target memory range} = \text{buf}[8] \dots \text{buf}[8 + (255 - 2) - 1] = \text{buf}[8] \dots \text{buf}[260]$$
  - `buf[260]` is **ONE BYTE BEYOND THE ALLOCATED BUFFER BOUNDARY**!
  - **Evaluation**: **SECURITY / BUFFER OVERFLOW DEFECT**. The condition MUST be `if (length < 2 || length > 254)`.

#### 2. Concurrent TCP Connection Handling
- **Requirement**: "A MODBUS/TCP server MUST support multiple concurrent TCP connections (typically at least 2 to 4 simultaneous connections)."
- **nanoMODBUS Implementation**: Single connection served sequentially.
- **Evaluation**: **PARTIALLY COMPLIANT**. Can be mitigated in Zephyr by transitioning `nanomodbus_z_server_tcp` to a non-blocking Reactor pattern.

---

### 3.3 Modbus Serial Line Specification and Implementation Guide v1.02

#### 1. Inter-Character ($t_{1.5}$) and Inter-Frame ($t_{3.5}$) Timings
- **Requirement**:
  - Baud $\le 19200$: $t_{1.5} = 1.5 \times 11 / \text{baud}$, $t_{3.5} = 3.5 \times 11 / \text{baud}$.
  - Baud $> 19200$: $t_{1.5} = 750\,\mu\text{s}$, $t_{3.5} = 1750\,\mu\text{s}$.
- **Evaluation**: **COMPLIANT**.
  - Formulas in `nmbs_z_serial_calc_timings` match the specification precisely:
    `16500000UL / baud_rate` and `38500000UL / baud_rate`.
  - In `nanomodbus_z_transport_serial.c`, `rx_put` discards partial frames if elapsed cycles exceed $t_{1.5}$.
  - In `nanomodbus_z_server_rtu.c`, `silence_timer` starts on character reception with timeout $t_{3.5}$, triggering request execution on silence expiration.

#### 2. RS-485 Transceiver Direction Control (Driver Enable)
- **Requirement**: Transceiver DE must be active during all transmitted bits and returned to RX mode after the stop bit of the last character.
- **Evaluation**: **COMPLIANT**.
  - Hardware DE (`UART_CFG_FLOW_CTRL_RS485`) supported.
  - Software GPIO DE supported with configurable `de_pre_delay_us` and `de_post_delay_us`.
  - When `CONFIG_UART_INTERRUPT_DRIVEN=y`, transport waits for TX FIFO drain before deasserting DE pin.
