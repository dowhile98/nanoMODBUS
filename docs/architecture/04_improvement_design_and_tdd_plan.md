# Improvement Design, Architectural Solutions & Step-by-Step TDD Plan

## 1. Categorization of Transferable Ideas (from Cyclone & Industrial Standards)

| Priority | Feature / Improvement | Rationale & Architectural Impact |
|---|---|---|
| **MUST HAVE** | **Eliminate VLAs in Core** | Eliminate stack hazards and MISRA C:2012 Rule 18.8 violations in FC 20 & FC 23. |
| **MUST HAVE** | **Fix TCP MBAP Off-By-One (length > 254)** | Fix 1-byte heap/stack buffer overflow when `length == 255`. |
| **MUST HAVE** | **Fix `nmbs_bitfield_write` Macro** | Sanitize values `((v) ? 1U : 0U)` to prevent adjacent bit corruption on multi-bit inputs. |
| **MUST HAVE** | **Expand Modbus Exceptions (05, 06, 08, 0A, 0B)** | Comply with Modbus Spec v1.1b3; prevent dropping valid standard exceptions as invalid responses. |
| **MUST HAVE** | **Multi-Threaded Target Address Safety** | Eliminate TOCTOU race condition in `nmbs_z_client` by supporting atomic per-request `unit_id`. |
| **SHOULD HAVE** | **Diagnostic Counters (Optional Kconfig)** | Track RX/TX frames, CRC errors, timeouts, and exception counts for production telemetry. |
| **SHOULD HAVE** | **Implement FC 22 (Mask Write Register)** | Common industrial automation function code for atomic bit masking on PLC registers. |
| **SHOULD HAVE** | **Server Table Lock/Unlock Callbacks** | Cyclone-style `lockCallback`/`unlockCallback` to synchronize Modbus table access across RTOS threads. |
| **NICE TO HAVE** | **Multi-Connection TCP Server Reactor** | Cyclone-style `socketPoll` reactor handling 2–4 clients concurrently without extra thread stacks. |
| **NICE TO HAVE** | **Secure Modbus/TCP (TLS)** | Port 802 encryption and authentication using Zephyr native TLS sockets. |
| **NICE TO HAVE** | **PDU Interception Callback** | Cyclone `processPduCallback` mechanism for proprietary function codes. |
| **NOT RECOMMENDED** | **Modbus ASCII Transport** | Obsolete 7-bit LRC protocol adding unnecessary flash and maintenance complexity. |
| **NOT RECOMMENDED** | **Dynamic Memory Allocation (malloc)** | Breaks zero-heap deterministic architecture of nanoMODBUS Zephyr module. |
| **NOT RECOMMENDED** | **Proprietary OSAL Wrappers** | Cyclone uses custom OS wrappers (`osCreateEvent`); nanoMODBUS must directly leverage Zephyr kernel/socket primitives. |

---

## 2. Architectural Designs for Core Improvements

### 2.1 Eliminating VLAs in Core (`nanomodbus.c`)
- **Root Cause**: Lines 1347, 1565, and 1608 use C99 Variable Length Arrays when `__STDC_NO_VLA__` is not defined:
  - `subreq[subreq_count]` in `handle_read_file_record`
  - `registers[byte_count_write / 2]` in `handle_read_write_registers`
  - `regs[read_quantity]` in `handle_read_write_registers`
- **Design Solution**:
  Modbus Application Protocol v1.1b3 strictly bounds these parameters:
  - Max subrequests in FC 20 = $245 / 7 = 35$ subrequests.
  - Max write registers in FC 23 = $0\text{x}007\text{B} = 123$ registers.
  - Max read registers in FC 23 = $0\text{x}007\text{D} = 125$ registers.
  Unconditionally declare fixed-size arrays:
  ```c
  /* In handle_read_file_record: */
  struct {
      uint8_t reference_type;
      uint16_t file_number;
      uint16_t record_number;
      uint16_t record_length;
  } subreq[35];

  /* In handle_read_write_registers: */
  uint16_t registers[0x007B]; // Max 123 registers (246 bytes)
  ...
  uint16_t regs[125];          // Max 125 registers (250 bytes)
  ```
- **Benefits**: Completely MISRA C:2012 Rule 18.8 compliant, deterministic stack footprint, zero compiler dependency.

---

### 2.2 Fixing Off-By-One Buffer Overflow in TCP MBAP Length Validation
- **Root Cause**:
  `struct nmbs_t` allocates `uint8_t buf[260];` (indices 0..259).
  In `recv_msg_header`:
  Indices 0..7 contain TID, PID, Length, Unit ID, FC.
  Remaining bytes are read to `nmbs->msg.buf + 8` for `length - 2` bytes.
  If `length == 255`:
  $\text{End offset} = 8 + (255 - 2) = 261 \implies \text{writes to index } 260$.
  Out-of-bounds write of 1 byte!
- **Modbus TCP Specification**:
  $\text{MBAP Length} = \text{Unit ID (1 byte)} + \text{PDU (max 253 bytes)} = 254 \text{ bytes maximum}$.
- **Design Solution**:
  In `nanomodbus.c`:
  ```c
  /* Replace: */
  if (length < 2 || length > 255)
      return NMBS_ERROR_INVALID_TCP_MBAP;

  /* With: */
  if (length < 2 || length > 254)
      return NMBS_ERROR_INVALID_TCP_MBAP;
  ```
  When `length == 254`: $8 + (254 - 2) = 260$ bytes consumed, occupying indices 0..259. Exactly fits inside `buf[260]`.

---

### 2.3 Correcting Bitfield Macro `nmbs_bitfield_write`
- **Root Cause**:
  `#define nmbs_bitfield_write(bf, b, v) ((bf)[(b) >> 3] = ((bf)[(b) >> 3] & ~(1 << ((b) & 7))) | ((v) << ((b) & 7)))`
  If `v` is not strictly `0` or `1` (for example `v = 2`, `v = 0xFF`, or `(flags & 0x04)`), `((v) << ((b) & 7))` shifts a multi-bit integer, corrupting adjacent bits in the byte.
- **Design Solution**:
  Sanitize `v` to exactly single-bit binary (`0U` or `1U`):
  ```c
  #define nmbs_bitfield_write(bf, b, v) \
      ((bf)[(b) >> 3] = (uint8_t) (((bf)[(b) >> 3] & ~(1U << ((b) & 7))) | (((v) ? 1U : 0U) << ((b) & 7))))
  ```

---

### 2.4 Expanding Standard Modbus Exception Codes and Error Mapping
- **Root Cause**:
  `nanomodbus.h` only enumerates exceptions 1..4. Codes 05, 06, 08, 0A, 0B are discarded as `NMBS_ERROR_INVALID_RESPONSE`.
- **Design Solution**:
  1. In `nanomodbus.h`:
     ```c
     typedef enum nmbs_error {
         /* Library errors -8 .. -1, 0 */
         ...
         // Modbus standard exceptions
         NMBS_EXCEPTION_ILLEGAL_FUNCTION = 1,
         NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS = 2,
         NMBS_EXCEPTION_ILLEGAL_DATA_VALUE = 3,
         NMBS_EXCEPTION_SERVER_DEVICE_FAILURE = 4,
         NMBS_EXCEPTION_ACKNOWLEDGE = 5,
         NMBS_EXCEPTION_SERVER_DEVICE_BUSY = 6,
         NMBS_EXCEPTION_MEMORY_PARITY_ERROR = 8,
         NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE = 10,
         NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 11,
     } nmbs_error;

     #define nmbs_error_is_exception(e) \
         (((e) >= 1 && (e) <= 6) || (e) == 8 || (e) == 10 || (e) == 11)
     ```
  2. In `nanomodbus.c`:
     In `recv_res_header`:
     ```c
     if (!nmbs_error_is_exception(exception))
         return NMBS_ERROR_INVALID_RESPONSE;
     ```
  3. In `zephyr/src/nanomodbus_z_common.c`:
     Map to standard POSIX errnos:
     - `NMBS_EXCEPTION_ILLEGAL_FUNCTION` $\to$ `-ENOSYS`
     - `NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS` $\to$ `-EFAULT`
     - `NMBS_EXCEPTION_ILLEGAL_DATA_VALUE` $\to$ `-EDOM`
     - `NMBS_EXCEPTION_SERVER_DEVICE_FAILURE` $\to$ `-EIO`
     - `NMBS_EXCEPTION_ACKNOWLEDGE` $\to$ `-EINPROGRESS`
     - `NMBS_EXCEPTION_SERVER_DEVICE_BUSY` $\to$ `-EBUSY`
     - `NMBS_EXCEPTION_MEMORY_PARITY_ERROR` $\to$ `-EILSEQ`
     - `NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE` $\to$ `-ENETUNREACH`
     - `NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND` $\to$ `-ETIMEDOUT`

---

### 2.5 Multi-Threaded Target Address Safety for Client
- **Root Cause**:
  `nmbs_z_client_set_target_address()` mutates `client->core.dest_address_rtu` under its own lock, releasing it before the subsequent request call. In multi-threaded environments, two threads interleaving `set_target_address` and `read_registers` cross-talk to each other's slave devices.
- **Design Solution**:
  Introduce atomic per-request functions:
  ```c
  int nmbs_z_client_req_read_holding_registers(nmbs_z_client_t *client, uint8_t unit_id,
                                              uint16_t address, uint16_t quantity,
                                              uint16_t *registers_out, k_timeout_t lock_timeout);
  ```
  Implementation:
  ```c
  int nmbs_z_client_req_read_holding_registers(nmbs_z_client_t *client, uint8_t unit_id,
                                              uint16_t address, uint16_t quantity,
                                              uint16_t *registers_out, k_timeout_t lock_timeout)
  {
      if (!client || !registers_out) return -EINVAL;
      int ret = client_lock(client, lock_timeout);
      if (ret != 0) return ret;

      nmbs_set_destination_rtu_address(&client->core, unit_id);
      nmbs_error err = nmbs_read_holding_registers(&client->core, address, quantity, registers_out);
      client_record_error(client, err);
      k_mutex_unlock(&client->lock);

      return nmbs_z_error_to_errno(err);
  }
  ```
  The legacy non-`req_` API simply forwards to the atomic implementation using `client->core.dest_address_rtu`.

---

### 2.6 Diagnostic Counters (`CONFIG_NANOMODBUS_DIAGNOSTICS`)
- **Structure**:
  ```c
  typedef struct nmbs_z_diag_counters {
      uint32_t rx_frames;
      uint32_t tx_frames;
      uint32_t crc_errors;
      uint32_t timeout_errors;
      uint32_t exception_errors;
      uint32_t transport_errors;
  } nmbs_z_diag_counters_t;
  ```
- **Kconfig Option**: `CONFIG_NANOMODBUS_DIAGNOSTICS` (default n), ensuring zero flash/RAM overhead when disabled.
- **Accessors**:
  `int nmbs_z_client_get_diag(const nmbs_z_client_t *client, nmbs_z_diag_counters_t *diag);`
  `int nmbs_z_client_clear_diag(nmbs_z_client_t *client);`

---

## 3. Step-by-Step TDD Implementation Plan for `firmware-dev`

```
Test-Driven Development Workflow:
  1. Write failing test (RED)
  2. Implement minimal architectural fix (GREEN)
  3. Verify against full regression suite (REFACTOR)
```

### Phase 1: Core Protocol Fixes (Host CMake / Unit Tests)
- [ ] **Step 1.1: Bitfield Macro TDD**
  - Add test in `tests/nanomodbus_tests.c`: `test_bitfield_multibit_write`.
  - Pass `v = 0xFE` or `v = 2` to `nmbs_bitfield_write(bf, 3, v)`. Assert only bit 3 is set; bits 2, 4, 5 are unaffected.
  - Verify RED.
  - Apply fix in `nanomodbus.h`.
  - Verify GREEN.
- [ ] **Step 1.2: TCP MBAP Length Validation TDD**
  - Add test in `tests/nanomodbus_tests.c`: `test_tcp_mbap_length_boundary`.
  - Inject frame with MBAP `length = 255`. Assert `nmbs_server_poll()` returns `NMBS_ERROR_INVALID_TCP_MBAP`.
  - Inject frame with MBAP `length = 254`. Assert valid processing without buffer overrun.
  - Verify RED.
  - Apply fix `if (length < 2 || length > 254)` in `nanomodbus.c`.
  - Verify GREEN.
- [ ] **Step 1.3: Eliminate VLAs**
  - Replace conditional VLA allocations with fixed bounds (`subreq[35]`, `registers[0x007B]`, `regs[125]`).
  - Run host tests with `-Wvla -Werror` compiler flags.
  - Verify clean build without warnings.
- [ ] **Step 1.4: Modbus Standard Exception Expansion TDD**
  - Add test in `tests/nanomodbus_tests.c`: `test_client_extended_exceptions`.
  - Mock server returning exception response `0x83` with code `0x06` (`SERVER_DEVICE_BUSY`).
  - Assert client returns `NMBS_EXCEPTION_SERVER_DEVICE_BUSY`.
  - Update `nanomodbus.h` enum and `nanomodbus.c` validation check.
  - Verify GREEN.

### Phase 2: Zephyr Adapter & Concurrency Enhancements (Ztest / native_sim)
- [ ] **Step 2.1: POSIX Error Mapping for Extended Exceptions**
  - Add test cases in `tests/zephyr/src/test_error_mapping.c`:
    - `NMBS_EXCEPTION_SERVER_DEVICE_BUSY` $\implies$ `-EBUSY`
    - `NMBS_EXCEPTION_ACKNOWLEDGE` $\implies$ `-EINPROGRESS`
    - `NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE` $\implies$ `-ENETUNREACH`
    - `NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND` $\implies$ `-ETIMEDOUT`
  - Update `nmbs_z_error_to_errno` and `nmbs_z_strerror` in `zephyr/src/nanomodbus_z_common.c`.
  - Run `./build_ztest/zephyr/zephyr.exe -test=nanomodbus_error::*`. Verify GREEN.
- [ ] **Step 2.2: Multi-Threaded Target Address Safety TDD**
  - Add test in `tests/zephyr/src/test_client_concurrency.c`: `test_client_atomic_unit_id_concurrency`.
  - Spawn 2 threads polling different slave addresses concurrently via `nmbs_z_client_req_read_holding_registers()`.
  - Assert zero address cross-talk under high contention.
  - Implement atomic `nmbs_z_client_req_*` functions in `nanomodbus_z_client.c`.
  - Verify GREEN.
- [ ] **Step 2.3: Diagnostic Telemetry TDD**
  - Add Kconfig `CONFIG_NANOMODBUS_DIAGNOSTICS` in `zephyr/Kconfig`.
  - Add unit test verifying that valid frames increment `rx_frames`/`tx_frames` and corrupted CRC increments `crc_errors`.
  - Implement counters in `nanomodbus_z_client.c` and `nanomodbus_z_server_rtu.c`.
  - Verify GREEN.

### Phase 3: Regression & Static Analysis Gate
- [ ] Execute full host test suite: `ctest --test-dir build_host --output-on-failure`.
- [ ] Execute native_sim Ztest suites:
  - `nanomodbus_baseline`
  - `nanomodbus_client`
  - `nanomodbus_error`
  - `nanomodbus_multi_instance`
  - `nanomodbus_serial`
  - `nanomodbus_server_rtu`
  - `nanomodbus_server_tcp`
- [ ] Build target sample for STM32H7:
  `west build -p always -b nucleo_h743zi samples/server_rtu -d build_server_rtu -- -DEXTRA_ZEPHYR_MODULES=$PWD`.
