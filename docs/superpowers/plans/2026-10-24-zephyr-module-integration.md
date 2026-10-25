# nanoMODBUS Zephyr RTOS External Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, verify, and validate nanoMODBUS as a native Zephyr RTOS external module with thread-safe client execution, event-driven RTU server (k_work), threaded TCP server (k_thread), and multi-instance gateway support.

**Architecture:** Encapsulate the pure C99 nanoMODBUS domain core inside a clean Ports & Adapters Zephyr module layer in `zephyr/`. Provide static-allocation thread-safe client wrappers (`k_mutex`), silence-timer RTU server handlers (`k_timer`/`k_work`), BSD socket TCP server runners (`k_thread`), and hardware adapters for Zephyr UART (with RS-485 DE pin control) and sockets.

**Tech Stack:** Zephyr RTOS v3.5+/v4.x, C99/C11, CMake 3.20+, Kconfig, Ztest, Unity Fixture, Zephyr BSD Sockets, Zephyr UART/GPIO drivers, west.

**Spec:** docs/superpowers/specs/2026-10-23-zephyr-module-integration-design.md

## Global Constraints
- Target workspace: `/home/jeffry/zephyrproject` with SDK `/home/jeffry/zephyr-sdk-1.0.1` and west in `/home/jeffry/.zephyr-venv`.
- Upstream purity: `nanomodbus.c` and `nanomodbus.h` remain 100% pure ISO C99 with zero Zephyr RTOS headers.
- Static allocation guarantee: Zero dynamic heap allocations (`k_malloc`); all synchronization primitives, buffers, and thread stacks are statically allocated.
- Concurrency safety: Client functions execute synchronously in caller thread context protected by `struct k_mutex`.
- Server RTU executes asynchronously via `struct k_work` upon t3.5 character silence detection via `struct k_timer`.
- Server TCP executes in dedicated Active Object `struct k_thread`.

## Review Focus
- [ ] Review Focus 1: Client lock timeout contention (caller returns `-EBUSY` or `-ETIMEDOUT` when mutex cannot be acquired, no deadlock). Test in Task 4.
- [ ] Review Focus 2: RS-485 DE pin premature deassertion before shift register drain (guard time or transmission complete check). Test in Task 3.
- [ ] Review Focus 3: RTU inter-frame silence false trigger on fragmented character delivery (timer reset on each character). Test in Task 5.
- [ ] Review Focus 4: TCP server unexpected socket abort/reset mid-stream (server recovers and returns cleanly to accept). Test in Task 6.
- [ ] Review Focus 5: Multi-instance isolation (exp: 2 RTU clients + 1 TCP server operate concurrently without memory or state cross-contamination). Test in Task 7.

---

### Task 1: Module Build System & Kconfig Baseline

**Files:**
- Create: `zephyr/Kconfig`
- Create: `zephyr/CMakeLists.txt`
- Modify: `zephyr/module.yml:1-5`
- Modify: `Kconfig:1-10`
- Modify: `CMakeLists.txt:15-25`
- Test: `tests/zephyr/CMakeLists.txt`
- Test: `tests/zephyr/prj.conf`
- Test: `tests/zephyr/testcase.yaml`
- Test: `tests/zephyr/src/test_build_baseline.c`

**Interfaces:**
- Consumes: `nanomodbus.h` (`nmbs_error`, `NMBS_ERROR_NONE`)
- Produces: Zephyr module registration `nanomodbus`, Kconfig menu `CONFIG_NANOMODBUS`, compile flags forwarding (`NMBS_SERVER_DISABLED`, `NMBS_CLIENT_DISABLED`, function code disabling flags)

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/testcase.yaml`:
```yaml
tests:
  nanomodbus.baseline:
    tags: modbus
    platform_allow: native_sim
    integration_platforms:
      - native_sim
    extra_configs:
      - CONFIG_ZTEST=y
      - CONFIG_NANOMODBUS=y
      - CONFIG_NANOMODBUS_CLIENT=y
      - CONFIG_NANOMODBUS_SERVER=y
```

Create `tests/zephyr/prj.conf`:
```ini
CONFIG_ZTEST=y
CONFIG_NANOMODBUS=y
CONFIG_NANOMODBUS_CLIENT=y
CONFIG_NANOMODBUS_SERVER=y
```

Create `tests/zephyr/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_nanomodbus_baseline)

target_sources(app PRIVATE src/test_build_baseline.c)
```

Create `tests/zephyr/src/test_build_baseline.c`:
```c
#include <zephyr/ztest.h>
#include <nanomodbus.h>

ZTEST(nanomodbus_baseline, test_module_symbols_available)
{
    zassert_true(IS_ENABLED(CONFIG_NANOMODBUS), "CONFIG_NANOMODBUS must be enabled");
    nmbs_error err = NMBS_ERROR_NONE;
    zassert_equal(err, 0, "NMBS_ERROR_NONE must evaluate to 0");
}

ZTEST_SUITE(nanomodbus_baseline, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL with CMake/Kconfig error: `CONFIG_NANOMODBUS was assigned the value 'y', but got the value 'n'` or `No module named nanomodbus`.

- [ ] **Step 3: Implement module build system and Kconfig**

Update `zephyr/module.yml`:
```yaml
name: nanomodbus
build:
  cmake: zephyr
  kconfig: zephyr/Kconfig
```

Update root `Kconfig`:
```kconfig
# SPDX-License-Identifier: MIT
# nanoMODBUS root Kconfig redirection
rsource "zephyr/Kconfig"
```

Update root `CMakeLists.txt:15-25`:
```cmake
if(ZEPHYR_BASE)
    add_subdirectory(zephyr)
else()
```

Create `zephyr/Kconfig` defining:
- `menuconfig NANOMODBUS`
- Logging integration template `subsys/logging/Kconfig.template.log_config`
- `NANOMODBUS_DEBUG` (enables `NMBS_DEBUG`)
- Roles: `NANOMODBUS_CLIENT` (default y), `NANOMODBUS_SERVER` (default y)
- Transports: `NANOMODBUS_TRANSPORT_SERIAL` (depends on `SERIAL`, default y), `NANOMODBUS_TRANSPORT_TCP` (depends on `NET_SOCKETS`, default n)
- Server Engines: `NANOMODBUS_SERVER_RTU`, `NANOMODBUS_SERVER_TCP`
- Buffer and Protocol Tuning: `NANOMODBUS_BITFIELD_MAX` (range 8..2000, default 2000), `NANOMODBUS_SERVER_RTU_RX_BUF_SIZE` (range 64..1024, default 260)
- Function code pruning symbols (`CONFIG_NANOMODBUS_FC_*_DISABLED`) per spec Section 3.4.

Create `zephyr/CMakeLists.txt`:
```cmake
# SPDX-License-Identifier: MIT

if(CONFIG_NANOMODBUS)
  zephyr_library()

  zephyr_include_directories(include)
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
  zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/../nanomodbus.c)

  if(NOT CONFIG_NANOMODBUS_SERVER)
    zephyr_library_compile_definitions(NMBS_SERVER_DISABLED)
  endif()

  if(NOT CONFIG_NANOMODBUS_CLIENT)
    zephyr_library_compile_definitions(NMBS_CLIENT_DISABLED)
  endif()

  if(CONFIG_NANOMODBUS_DEBUG)
    zephyr_library_compile_definitions(NMBS_DEBUG)
  endif()
endif()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS && ./build/zephyr/zephyr.exe"`
Expected: PASS (Output: `PROJECT EXECUTION SUCCESSFUL`, 1 test passed).

- [ ] **Step 5: Commit**

```bash
git add zephyr/module.yml zephyr/Kconfig zephyr/CMakeLists.txt Kconfig CMakeLists.txt tests/zephyr/
git commit -m "build(zephyr): initialize Zephyr external module build system and Kconfig"
```

---

### Task 2: Error Mapping & Interface Contracts

**Files:**
- Create: `zephyr/include/zephyr/nanomodbus/nanomodbus_z.h`
- Create: `zephyr/src/nanomodbus_z_common.c`
- Modify: `zephyr/CMakeLists.txt`
- Modify: `tests/zephyr/CMakeLists.txt`
- Create: `tests/zephyr/src/test_error_mapping.c`

**Interfaces:**
- Consumes: `nmbs_error` from `nanomodbus.h`
- Produces:
  - `int nmbs_z_error_to_errno(nmbs_error err);`
  - `const char *nmbs_z_strerror(nmbs_error err);`

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_error_mapping.c`:
```c
#include <zephyr/ztest.h>
#include <errno.h>
#include <nanomodbus.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>

ZTEST(nanomodbus_error, test_error_translation_matrix)
{
    /* Success */
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_NONE), 0, "NMBS_ERROR_NONE -> 0");

    /* Internal errors mapped to negative POSIX errno */
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_ARGUMENT), -EINVAL, "-1 -> -EINVAL");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_RESPONSE), -EBADMSG, "-2 -> -EBADMSG");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_TIMEOUT), -ETIMEDOUT, "-3 -> -ETIMEDOUT");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_TRANSPORT), -EIO, "-4 -> -EIO");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_CRC), -EBADMSG, "-5 -> -EBADMSG");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_TCP_MBAP), -EPROTO, "-6 -> -EPROTO");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_UNIT_ID), -ENODEV, "-7 -> -ENODEV");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_REQUEST), -EINVAL, "-8 -> -EINVAL");

    /* Modbus Exceptions (positive values) */
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_FUNCTION), -ENOSYS, "1 -> -ENOSYS");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS), -EFAULT, "2 -> -EFAULT");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_DATA_VALUE), -EDOM, "3 -> -EDOM");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_SERVER_DEVICE_FAILURE), -EIO, "4 -> -EIO");

    /* Unknown code fallback */
    zassert_equal(nmbs_z_error_to_errno((nmbs_error)-999), -EINVAL, "unknown -> -EINVAL");

    /* String representation */
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_NONE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_TIMEOUT), "strerror must not be NULL");
}

ZTEST_SUITE(nanomodbus_error, NULL, NULL, NULL, NULL, NULL);
```

Add `src/test_error_mapping.c` to `tests/zephyr/CMakeLists.txt`:
```cmake
target_sources(app PRIVATE src/test_build_baseline.c src/test_error_mapping.c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL with compilation error: `zephyr/nanomodbus/nanomodbus_z.h: No such file or directory` or `undefined reference to nmbs_z_error_to_errno`.

- [ ] **Step 3: Implement `nanomodbus_z.h` and `nanomodbus_z_common.c`**

Create `zephyr/include/zephyr/nanomodbus/nanomodbus_z.h`:
- Header guard `ZEPHYR_INCLUDE_NANOMODBUS_Z_H_`
- `#include <stdint.h>`, `<stdbool.h>`, `<errno.h>`, `<zephyr/kernel.h>`, `<nanomodbus.h>`
- Prototypes: `int nmbs_z_error_to_errno(nmbs_error err);` and `const char *nmbs_z_strerror(nmbs_error err);`
- Definition of `struct nmbs_z_client` with `nmbs_t core`, `nmbs_platform_conf platform`, `struct k_mutex lock` guarded by `CONFIG_NANOMODBUS_CLIENT`.

Create `zephyr/src/nanomodbus_z_common.c`:
Implement `nmbs_z_error_to_errno()` switch-case matching spec Section 7.1 and `nmbs_z_strerror()` returning descriptive string literals.

Update `zephyr/CMakeLists.txt` to add:
```cmake
zephyr_library_sources(src/nanomodbus_z_common.c)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS && ./build/zephyr/zephyr.exe"`
Expected: PASS (`test_error_translation_matrix: PASS`).

- [ ] **Step 5: Commit**

```bash
git add zephyr/include/zephyr/nanomodbus/nanomodbus_z.h zephyr/src/nanomodbus_z_common.c zephyr/CMakeLists.txt tests/zephyr/
git commit -m "feat(zephyr): add error translation matrix and public contract nanomodbus_z.h"
```

---

### Task 3: Serial RTU Transport Adapter & RS-485 DE Control

**Files:**
- Create: `zephyr/include/zephyr/nanomodbus/nanomodbus_z_serial.h`
- Create: `zephyr/src/nanomodbus_z_transport_serial.c`
- Modify: `zephyr/CMakeLists.txt`
- Modify: `tests/zephyr/CMakeLists.txt`
- Create: `tests/zephyr/src/test_rs485_timing.c`

**Interfaces:**
- Consumes: `<zephyr/drivers/uart.h>`, `<zephyr/drivers/gpio.h>`, `<zephyr/sys/ring_buffer.h>`, `nanomodbus.h` (`nmbs_platform_conf`)
- Produces:
  - `nmbs_z_serial_transport_config_t`
  - `nmbs_z_serial_transport_t`
  - `int nmbs_z_serial_transport_init(nmbs_z_serial_transport_t *transport, const nmbs_z_serial_transport_config_t *config, nmbs_platform_conf *platform_conf);`
  - `int32_t nmbs_z_transport_serial_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);`
  - `int32_t nmbs_z_transport_serial_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);`

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_rs485_timing.c`:
- Pins Review Focus 2: Verify DE pin is asserted BEFORE byte 0 starts, DE remains asserted until the final stop bit drains from the shift register, and DE deasserts only AFTER post-delay.
```c
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

/* Mock GPIO and UART state capture */
static struct {
    int de_state;
    uint32_t de_assert_count;
    uint32_t de_deassert_count;
    bool de_held_during_tx;
    bool premature_deassert_detected;
} mock_de_tracker;

/* Timing verification tests */
ZTEST(nanomodbus_serial, test_baud_silence_calculations)
{
    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg_9600 = {
        .uart_dev = NULL,
        .baud_rate = 9600,
        .de_pre_delay_us = 50,
        .de_post_delay_us = 50,
    };
    nmbs_platform_conf p_conf;

    int ret = nmbs_z_serial_transport_init(&transport, &cfg_9600, &p_conf);
    zassert_equal(ret, 0, "transport init 9600 must succeed");
    /* 9600 bps: 11 bits = 1145.8 us; t1.5 = 1718 us; t3.5 = 4010 us */
    zassert_between_inclusive(transport.t1_5_us, 1700, 1730, "t1.5 for 9600 bps");
    zassert_between_inclusive(transport.t3_5_us, 4000, 4030, "t3.5 for 9600 bps");

    nmbs_z_serial_transport_config_t cfg_115200 = {
        .uart_dev = NULL,
        .baud_rate = 115200,
        .de_pre_delay_us = 50,
        .de_post_delay_us = 50,
    };
    ret = nmbs_z_serial_transport_init(&transport, &cfg_115200, &p_conf);
    zassert_equal(ret, 0, "transport init 115200 must succeed");
    /* > 19200 bps: fixed t1.5 = 750 us, fixed t3.5 = 1750 us */
    zassert_equal(transport.t1_5_us, 750, "t1.5 fixed at 750 us for 115200");
    zassert_equal(transport.t3_5_us, 1750, "t3.5 fixed at 1750 us for 115200");
}

ZTEST(nanomodbus_serial, test_rs485_de_pin_drain_timing)
{
    /* Review Focus 2: Ensure DE pin remains asserted until shift register drain */
    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = NULL,
        .baud_rate = 115200,
        .de_pre_delay_us = 100,
        .de_post_delay_us = 100,
    };
    nmbs_platform_conf p_conf;
    nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);

    uint8_t payload[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    mock_de_tracker.premature_deassert_detected = false;

    int32_t written = p_conf.transport.write(payload, sizeof(payload), 1000, &transport);
    zassert_equal(written, sizeof(payload), "All bytes written");
    zassert_false(mock_de_tracker.premature_deassert_detected,
                  "DE pin must never deassert prematurely before shift register drain");
}

ZTEST_SUITE(nanomodbus_serial, NULL, NULL, NULL, NULL, NULL);
```

Add `src/test_rs485_timing.c` to `tests/zephyr/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL with `zephyr/nanomodbus/nanomodbus_z_serial.h: No such file or directory` or `undefined reference to nmbs_z_serial_transport_init`.

- [ ] **Step 3: Implement `nanomodbus_z_serial.h` and `nanomodbus_z_transport_serial.c`**

Create `zephyr/include/zephyr/nanomodbus/nanomodbus_z_serial.h`:
- Definitions of `nmbs_z_serial_transport_config_t` and `nmbs_z_serial_transport_t`.
- Function prototypes: `nmbs_z_serial_transport_init()`, `nmbs_z_transport_serial_read()`, `nmbs_z_transport_serial_write()`.

Create `zephyr/src/nanomodbus_z_transport_serial.c`:
- Timing calculation:
  For baud $\le 19200$: $t_{1.5} = \frac{16500000}{Baud}$, $t_{3.5} = \frac{38500000}{Baud}$.
  For baud $> 19200$: $t_{1.5} = 750 \ \mu\text{s}$, $t_{3.5} = 1750 \ \mu\text{s}$.
- RS-485 DE Control:
  1. If `cfg->de_gpio.port != NULL`: assert DE `gpio_pin_set_dt(&cfg->de_gpio, 1)`.
  2. Guard wait `k_busy_wait(cfg->de_pre_delay_us)`.
  3. Transmit payload bytes to UART.
  4. Wait for UART TX complete (`uart_irq_tx_complete()` or transmitter idle check).
  5. Guard wait `k_busy_wait(cfg->de_post_delay_us)`.
  6. Deassert DE `gpio_pin_set_dt(&cfg->de_gpio, 0)`.
- Platform read/write callback bindings: populate `platform_conf->transport.read` and `platform_conf->transport.write`.

Update `zephyr/CMakeLists.txt` to include `src/nanomodbus_z_transport_serial.c` conditionally on `CONFIG_NANOMODBUS_TRANSPORT_SERIAL`.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS && ./build/zephyr/zephyr.exe"`
Expected: PASS (`test_baud_silence_calculations: PASS`, `test_rs485_de_pin_drain_timing: PASS`).

- [ ] **Step 5: Commit**

```bash
git add zephyr/include/zephyr/nanomodbus/nanomodbus_z_serial.h zephyr/src/nanomodbus_z_transport_serial.c zephyr/CMakeLists.txt tests/zephyr/
git commit -m "feat(zephyr): implement serial RTU transport adapter with RS-485 DE timing guard"
```

---

### Task 4: Thread-Safe Client Wrapper

**Files:**
- Create: `zephyr/src/nanomodbus_z_client.c`
- Modify: `zephyr/include/zephyr/nanomodbus/nanomodbus_z.h`
- Modify: `zephyr/CMakeLists.txt`
- Modify: `tests/zephyr/CMakeLists.txt`
- Create: `tests/zephyr/src/test_client_concurrency.c`

**Interfaces:**
- Consumes: `nanomodbus.h` (`nmbs_client_create`, `nmbs_read_holding_registers`, etc.), `k_mutex_init`, `k_mutex_lock`, `k_mutex_unlock` from `<zephyr/kernel.h>`, `nmbs_z_error_to_errno` from `nanomodbus_z.h`
- Produces:
  - `int nmbs_z_client_init(nmbs_z_client_t *client, const nmbs_platform_conf *transport);`
  - `int nmbs_z_client_set_target_address(nmbs_z_client_t *client, uint8_t unit_id, k_timeout_t timeout);`
  - `int nmbs_z_client_read_holding_registers(...)`
  - `int nmbs_z_client_read_input_registers(...)`
  - `int nmbs_z_client_read_coils(...)`
  - `int nmbs_z_client_read_discrete_inputs(...)`
  - `int nmbs_z_client_write_single_coil(...)`
  - `int nmbs_z_client_write_single_register(...)`
  - `int nmbs_z_client_write_multiple_coils(...)`
  - `int nmbs_z_client_write_multiple_registers(...)`

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_client_concurrency.c`:
- Pins Review Focus 1: Client lock contention test returning `-EBUSY` or `-ETIMEDOUT` without deadlock when another thread holds mutex.
```c
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>

static int32_t mock_transport_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg) {
    ARG_UNUSED(buf); ARG_UNUSED(byte_timeout_ms); ARG_UNUSED(arg);
    return count;
}

static int32_t mock_transport_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg) {
    ARG_UNUSED(buf); ARG_UNUSED(byte_timeout_ms); ARG_UNUSED(arg);
    return count;
}

ZTEST(nanomodbus_client, test_client_lock_contention_timeout)
{
    /* Review Focus 1: Verify caller returns -EBUSY or -ETIMEDOUT on lock contention */
    nmbs_z_client_t client;
    nmbs_platform_conf platform = {0};
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "client init must succeed");

    /* Manually hold the mutex to simulate a long-running caller thread */
    ret = k_mutex_lock(&client.lock, K_NO_WAIT);
    zassert_equal(ret, 0, "must acquire lock initially");

    /* Attempt to call client API with K_NO_WAIT */
    uint16_t reg_val = 0;
    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, &reg_val, K_NO_WAIT);
    zassert_true(ret == -EBUSY || ret == -ETIMEDOUT,
                 "Expected -EBUSY or -ETIMEDOUT on contention, got %d", ret);

    /* Release lock */
    k_mutex_unlock(&client.lock);

    /* Now call with sufficient timeout - lock must succeed */
    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "set address should succeed when lock is free");
}

ZTEST_SUITE(nanomodbus_client, NULL, NULL, NULL, NULL, NULL);
```

Add `src/test_client_concurrency.c` to `tests/zephyr/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL with `undefined reference to nmbs_z_client_init` and `nmbs_z_client_read_holding_registers`.

- [ ] **Step 3: Implement `nanomodbus_z_client.c`**

Create `zephyr/src/nanomodbus_z_client.c`:
- `nmbs_z_client_init(nmbs_z_client_t *client, const nmbs_platform_conf *transport)`:
  - Check non-null args (`-EINVAL`).
  - Copy `transport` into `client->platform`.
  - Initialize mutex `k_mutex_init(&client->lock)`.
  - Call `nmbs_client_create(&client->core, &client->platform)`.
- Client wrapper pattern for every FC:
  ```c
  int nmbs_z_client_read_holding_registers(nmbs_z_client_t *client, uint16_t address,
                                          uint16_t quantity, uint16_t *registers_out,
                                          k_timeout_t lock_timeout)
  {
      if (!client || !registers_out) return -EINVAL;
      int ret = k_mutex_lock(&client->lock, lock_timeout);
      if (ret != 0) return ret; /* returns -EBUSY or -EAGAIN/-ETIMEDOUT from Zephyr kernel */

      nmbs_error err = nmbs_read_holding_registers(&client->core, address, quantity, registers_out);
      k_mutex_unlock(&client->lock);
      return nmbs_z_error_to_errno(err);
  }
  ```
- Implement all specified functions: `read_input_registers`, `read_coils`, `read_discrete_inputs`, `write_single_coil`, `write_single_register`, `write_multiple_coils`, `write_multiple_registers`, `set_target_address`.

Update `zephyr/CMakeLists.txt` to compile `src/nanomodbus_z_client.c` when `CONFIG_NANOMODBUS_CLIENT=y`.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS && ./build/zephyr/zephyr.exe"`
Expected: PASS (`test_client_lock_contention_timeout: PASS`).

- [ ] **Step 5: Commit**

```bash
git add zephyr/src/nanomodbus_z_client.c zephyr/include/zephyr/nanomodbus/nanomodbus_z.h zephyr/CMakeLists.txt tests/zephyr/
git commit -m "feat(zephyr): implement thread-safe client wrapper with deterministic mutex contention"
```

---

### Task 5: Event-Driven RTU Server Engine

**Files:**
- Create: `zephyr/src/nanomodbus_z_server_rtu.c`
- Modify: `zephyr/include/zephyr/nanomodbus/nanomodbus_z_serial.h`
- Modify: `zephyr/CMakeLists.txt`
- Modify: `tests/zephyr/CMakeLists.txt`
- Create: `tests/zephyr/src/test_server_rtu.c`

**Interfaces:**
- Consumes: `nanomodbus.h` (`nmbs_server_create`, `nmbs_server_poll`), `k_timer`, `k_work`, `k_work_submit` from `<zephyr/kernel.h>`, `nanomodbus_z_serial.h`
- Produces:
  - `nmbs_z_server_rtu_t`
  - `int nmbs_z_server_rtu_init(nmbs_z_server_rtu_t *server, const nmbs_z_serial_transport_config_t *config, const nmbs_callbacks *callbacks, uint8_t server_address);`
  - Silence timer expiry handler submitting `server->work`
  - Workqueue execution handler calling `nmbs_server_poll(&server->core)`

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_server_rtu.c`:
- Pins Review Focus 3: RTU inter-frame silence false trigger test. Feed bytes fragmented with intervals `< t3.5`; verify timer resets and workqueue is NOT triggered until genuine `>= t3.5` frame gap occurs.
```c
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

static uint32_t work_execution_count = 0;
static nmbs_error mock_read_holding_registers(uint16_t address, uint16_t quantity,
                                             uint16_t *registers_out, uint8_t unit_id,
                                             void *arg) {
    ARG_UNUSED(address); ARG_UNUSED(unit_id); ARG_UNUSED(arg);
    for (int i = 0; i < quantity; i++) registers_out[i] = 42 + i;
    return NMBS_ERROR_NONE;
}

ZTEST(nanomodbus_server_rtu, test_fragmented_character_silence_reset)
{
    /* Review Focus 3: Silence timer reset prevents premature trigger */
    nmbs_z_server_rtu_t server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = NULL,
        .baud_rate = 115200, /* t3.5 = 1750 us */
        .de_pre_delay_us = 50,
        .de_post_delay_us = 50,
    };
    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_rtu_init(&server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "server rtu init must succeed");

    /* Simulate feeding 5 bytes with 500 us interval (< 1750 us t3.5) */
    for (int i = 0; i < 5; i++) {
        k_timer_start(&server.silence_timer, K_USEC(server.transport.t3_5_us), K_NO_WAIT);
        k_sleep(K_USEC(500));
        zassert_equal(k_timer_status_get(&server.silence_timer), 0,
                      "Silence timer must not expire while characters stream with gap < t3.5");
    }

    /* Now allow line to go silent for > t3.5 */
    k_sleep(K_USEC(2500));
    /* Timer must have expired and submitted work */
    zassert_true(k_timer_status_get(&server.silence_timer) > 0 || k_work_is_pending(&server.work) || work_execution_count > 0,
                 "Silence timer must expire after full t3.5 silence interval");
}

ZTEST_SUITE(nanomodbus_server_rtu, NULL, NULL, NULL, NULL, NULL);
```

Add `src/test_server_rtu.c` to `tests/zephyr/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL with `undefined reference to nmbs_z_server_rtu_init`.

- [ ] **Step 3: Implement `nanomodbus_z_server_rtu.c`**

Create `zephyr/src/nanomodbus_z_server_rtu.c`:
- Static timer expiry handler:
  ```c
  static void on_silence_timer_expired(struct k_timer *timer) {
      nmbs_z_server_rtu_t *server = CONTAINER_OF(timer, nmbs_z_server_rtu_t, silence_timer);
      k_work_submit(&server->work);
  }
  ```
- Static workqueue handler:
  ```c
  static void on_server_work(struct k_work *work) {
      nmbs_z_server_rtu_t *server = CONTAINER_OF(work, nmbs_z_server_rtu_t, work);
      nmbs_server_poll(&server->core);
  }
  ```
- `nmbs_z_server_rtu_init()`:
  - Initialize serial transport `nmbs_z_serial_transport_init(&server->transport, config, &platform)`.
  - Initialize nanoMODBUS core `nmbs_server_create(&server->core, server_address, &platform, callbacks)`.
  - Initialize `k_timer_init(&server->silence_timer, on_silence_timer_expired, NULL)`.
  - Initialize `k_work_init(&server->work, on_server_work)`.
  - Set server unit address.

Update `zephyr/CMakeLists.txt` to compile `src/nanomodbus_z_server_rtu.c` when `CONFIG_NANOMODBUS_SERVER_RTU=y`.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS && ./build/zephyr/zephyr.exe"`
Expected: PASS (`test_fragmented_character_silence_reset: PASS`).

- [ ] **Step 5: Commit**

```bash
git add zephyr/src/nanomodbus_z_server_rtu.c zephyr/include/zephyr/nanomodbus/nanomodbus_z_serial.h zephyr/CMakeLists.txt tests/zephyr/
git commit -m "feat(zephyr): implement event-driven RTU server engine with t3.5 silence detection"
```

---

### Task 6: Active Object TCP Server & Client Socket Adapter

**Files:**
- Create: `zephyr/include/zephyr/nanomodbus/nanomodbus_z_tcp.h`
- Create: `zephyr/src/nanomodbus_z_transport_tcp.c`
- Create: `zephyr/src/nanomodbus_z_server_tcp.c`
- Modify: `zephyr/CMakeLists.txt`
- Modify: `tests/zephyr/CMakeLists.txt`
- Create: `tests/zephyr/src/test_server_tcp.c`

**Interfaces:**
- Consumes: `<zephyr/net/socket.h>` (`zsock_*`), `nmbs_server_create`, `nmbs_server_poll`, `nmbs_client_create` from `nanomodbus.h`, `k_thread_create`, `k_event` from `<zephyr/kernel.h>`
- Produces:
  - `nmbs_z_tcp_transport_t`
  - `int nmbs_z_tcp_transport_connect(nmbs_z_tcp_transport_t *transport, const char *server_ip, uint16_t port, int32_t timeout_ms, nmbs_platform_conf *platform_conf);`
  - `int nmbs_z_tcp_transport_close(nmbs_z_tcp_transport_t *transport);`
  - `nmbs_z_server_tcp_t`
  - `int nmbs_z_server_tcp_start(nmbs_z_server_tcp_t *server, const nmbs_callbacks *callbacks, uint16_t port);`
  - `int nmbs_z_server_tcp_stop(nmbs_z_server_tcp_t *server);`

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_server_tcp.c`:
- Pins Review Focus 4: TCP server unexpected socket abort/reset mid-stream. Verifies server cleans up connection state and returns to accept loop without resource leak or hanging.
```c
#include <zephyr/ztest.h>
#include <zephyr/net/socket.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>

static nmbs_error mock_tcp_read_holding_registers(uint16_t address, uint16_t quantity,
                                                 uint16_t *registers_out, uint8_t unit_id,
                                                 void *arg) {
    ARG_UNUSED(address); ARG_UNUSED(unit_id); ARG_UNUSED(arg);
    for (int i = 0; i < quantity; i++) registers_out[i] = 100 + i;
    return NMBS_ERROR_NONE;
}

ZTEST(nanomodbus_server_tcp, test_tcp_server_client_abort_recovery)
{
    /* Review Focus 4: Abrupt client abort mid-stream recovery */
    nmbs_z_server_tcp_t server;
    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_tcp_read_holding_registers;

    int ret = nmbs_z_server_tcp_start(&server, &cbs, 5502);
    zassert_equal(ret, 0, "Server TCP start must succeed");

    /* Create client socket and connect */
    int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    zassert_true(sock >= 0, "client socket created");

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(5502),
    };
    zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    ret = zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    zassert_equal(ret, 0, "client connected to server");

    /* Abruptly terminate client socket to simulate link drop / reset mid-stream */
    zsock_close(sock);

    /* Allow server thread state machine to detect disconnection and return to listening */
    k_sleep(K_MSEC(100));

    /* Verify server is still alive and accepting subsequent connections */
    int sock2 = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    zassert_true(sock2 >= 0, "second client socket created");
    ret = zsock_connect(sock2, (struct sockaddr *)&addr, sizeof(addr));
    zassert_equal(ret, 0, "second client connected cleanly after previous abort");

    zsock_close(sock2);
    ret = nmbs_z_server_tcp_stop(&server);
    zassert_equal(ret, 0, "Server TCP stopped cleanly");
}

ZTEST_SUITE(nanomodbus_server_tcp, NULL, NULL, NULL, NULL, NULL);
```

Add `src/test_server_tcp.c` to `tests/zephyr/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL with `zephyr/nanomodbus/nanomodbus_z_tcp.h: No such file or directory` or `undefined reference to nmbs_z_server_tcp_start`.

- [ ] **Step 3: Implement `nanomodbus_z_tcp.h`, `nanomodbus_z_transport_tcp.c`, and `nanomodbus_z_server_tcp.c`**

Create `zephyr/include/zephyr/nanomodbus/nanomodbus_z_tcp.h`:
- Definitions of `nmbs_z_tcp_transport_t` and `nmbs_z_server_tcp_t`.
- Stack definition using `K_KERNEL_STACK_MEMBER(stack, CONFIG_NANOMODBUS_SERVER_TCP_STACK_SIZE)`.
- Prototypes: `nmbs_z_tcp_transport_connect`, `nmbs_z_tcp_transport_close`, `nmbs_z_server_tcp_start`, `nmbs_z_server_tcp_stop`.

Create `zephyr/src/nanomodbus_z_transport_tcp.c`:
- `nmbs_z_transport_tcp_read`: loops `zsock_recv()`, handles timeout and errors.
- `nmbs_z_transport_tcp_write`: loops `zsock_send()`, handles errors.
- `nmbs_z_tcp_transport_connect`: parses server IP, creates socket, sets `SO_RCVTIMEO`/`SO_SNDTIMEO`, binds callbacks into `platform_conf`.
- `nmbs_z_tcp_transport_close`: closes socket and resets descriptor.

Create `zephyr/src/nanomodbus_z_server_tcp.c`:
- Active Object thread function `tcp_server_thread_fn()`:
  - Lifecycle state machine: `STATE_INIT` -> `STATE_BIND_LISTEN` -> `STATE_ACCEPTING` -> `STATE_CONNECTED` -> `STATE_DISCONNECTING`.
  - In `STATE_CONNECTED`: call `nmbs_server_poll(&server->core)`.
  - If client disconnects or aborts (`zsock_recv` returns 0 or `-ECONNRESET`/`-ETIMEDOUT`), close `client_sock` and transition back to `STATE_ACCEPTING`.
  - Check `k_event` for stop signal; on stop, close all sockets and terminate cleanly.

Update `zephyr/CMakeLists.txt` to compile `src/nanomodbus_z_transport_tcp.c` and `src/nanomodbus_z_server_tcp.c` when `CONFIG_NANOMODBUS_SERVER_TCP=y`.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: PASS (`test_tcp_server_client_abort_recovery: PASS`).

- [ ] **Step 5: Commit**

```bash
git add zephyr/include/zephyr/nanomodbus/nanomodbus_z_tcp.h zephyr/src/nanomodbus_z_transport_tcp.c zephyr/src/nanomodbus_z_server_tcp.c zephyr/CMakeLists.txt tests/zephyr/
git commit -m "feat(zephyr): implement Active Object TCP server and socket transport adapter"
```

---

### Task 7: Multi-Instance Gateway & Application Samples

**Files:**
- Create: `examples/server_rtu/CMakeLists.txt`
- Create: `examples/server_rtu/prj.conf`
- Create: `examples/server_rtu/boards/native_sim.overlay`
- Create: `examples/server_rtu/src/main.c`
- Create: `examples/client_tcp/CMakeLists.txt`
- Create: `examples/client_tcp/prj.conf`
- Create: `examples/client_tcp/src/main.c`
- Create: `examples/modbus_gateway/CMakeLists.txt`
- Create: `examples/modbus_gateway/prj.conf`
- Create: `examples/modbus_gateway/boards/native_sim.overlay`
- Create: `examples/modbus_gateway/src/main.c`
- Create: `tests/zephyr/src/test_multi_instance_gateway.c`
- Modify: `tests/zephyr/CMakeLists.txt`
- Modify: `tests/zephyr/testcase.yaml`

**Interfaces:**
- Consumes: All modules from Tasks 1-6 (`nmbs_z_client_t`, `nmbs_z_server_rtu_t`, `nmbs_z_server_tcp_t`, serial & TCP transport headers)
- Produces: Concentrator Gateway application, standalone RTU server sample, standalone TCP client sample, multi-instance isolation test

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_multi_instance_gateway.c`:
- Pins Review Focus 5: Multi-instance isolation. Run 2 independent RTU clients targeting different server addresses + 1 TCP server concurrently. Verify data flows independently with zero cross-talk or lock interference.
```c
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>

/* Independent state storage for 2 RTU slaves */
static uint16_t slave1_regs[4] = {0x1111, 0x1112, 0x1113, 0x1114};
static uint16_t slave2_regs[4] = {0x2221, 0x2222, 0x2223, 0x2224};

ZTEST(nanomodbus_gateway, test_multi_instance_isolation)
{
    /* Review Focus 5: Verify 2 clients + 1 server run concurrently without interference */
    nmbs_z_client_t client_rtu_1;
    nmbs_z_client_t client_rtu_2;
    nmbs_platform_conf p1 = {0}, p2 = {0};

    p1.transport = NMBS_TRANSPORT_RTU;
    p2.transport = NMBS_TRANSPORT_RTU;

    int ret1 = nmbs_z_client_init(&client_rtu_1, &p1);
    int ret2 = nmbs_z_client_init(&client_rtu_2, &p2);

    zassert_equal(ret1, 0, "client 1 init");
    zassert_equal(ret2, 0, "client 2 init");

    /* Verify independent target addresses */
    nmbs_z_client_set_target_address(&client_rtu_1, 1, K_MSEC(100));
    nmbs_z_client_set_target_address(&client_rtu_2, 2, K_MSEC(100));

    zassert_equal(client_rtu_1.core.address_rtu, 1, "client 1 target address is 1");
    zassert_equal(client_rtu_2.core.address_rtu, 2, "client 2 target address is 2");

    /* Verify mutexes are independent */
    ret1 = k_mutex_lock(&client_rtu_1.lock, K_NO_WAIT);
    zassert_equal(ret1, 0, "client 1 lock acquired");

    /* Client 2 lock must still be freely acquirable */
    ret2 = k_mutex_lock(&client_rtu_2.lock, K_NO_WAIT);
    zassert_equal(ret2, 0, "client 2 lock must not be blocked by client 1");

    k_mutex_unlock(&client_rtu_1.lock);
    k_mutex_unlock(&client_rtu_2.lock);
}

ZTEST_SUITE(nanomodbus_gateway, NULL, NULL, NULL, NULL, NULL);
```

Add `src/test_multi_instance_gateway.c` to `tests/zephyr/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: FAIL if `test_multi_instance_gateway.c` fails or builds with errors before sample additions.

- [ ] **Step 3: Implement sample applications and complete Twister harness**

1. Implement `examples/server_rtu`:
   - `CMakeLists.txt`: standard Zephyr application CMake targeting `app`.
   - `prj.conf`: enables `CONFIG_NANOMODBUS=y`, `CONFIG_NANOMODBUS_SERVER=y`, `CONFIG_NANOMODBUS_SERVER_RTU=y`, `CONFIG_SERIAL=y`.
   - `boards/native_sim.overlay`: UART and RS-485 DE GPIO overlay for native simulation.
   - `src/main.c`: exposes holding registers 0–9 holding uptime seconds, CPU frequency, and LED status.

2. Implement `examples/client_tcp`:
   - `CMakeLists.txt`: standard Zephyr application CMake targeting `app`.
   - `prj.conf`: enables `CONFIG_NANOMODBUS=y`, `CONFIG_NANOMODBUS_CLIENT=y`, `CONFIG_NANOMODBUS_TRANSPORT_TCP=y`, `CONFIG_NETWORKING=y`, `CONFIG_NET_SOCKETS=y`.
   - `src/main.c`: connects to Modbus TCP server, polls 10 registers at 1 Hz, logs telemetry.

3. Implement `examples/modbus_gateway`:
   - `CMakeLists.txt`: standard Zephyr application CMake targeting `app`.
   - `prj.conf`: enables `CONFIG_NANOMODBUS=y`, `CONFIG_NANOMODBUS_CLIENT=y`, `CONFIG_NANOMODBUS_SERVER=y`, `CONFIG_NANOMODBUS_TRANSPORT_SERIAL=y`, `CONFIG_NANOMODBUS_SERVER_TCP=y`.
   - `boards/native_sim.overlay`: dual UART overlays.
   - `src/main.c`: initializes 2 RTU clients on independent UARTs, polls slave devices, caches register data, and serves it over Modbus TCP Server on port 502.

4. Update `tests/zephyr/testcase.yaml` configuring complete Twister test matrix:
   - `nanomodbus.baseline`
   - `nanomodbus.error_mapping`
   - `nanomodbus.rs485_timing`
   - `nanomodbus.client_concurrency`
   - `nanomodbus.server_rtu`
   - `nanomodbus.server_tcp`
   - `nanomodbus.multi_instance_gateway`

- [ ] **Step 4: Run test to verify it passes**

Run test binary:
`bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim tests/zephyr -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS && ./build/zephyr/zephyr.exe"`
Expected: PASS (All test suites pass).

Verify sample builds:
`bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim examples/server_rtu -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
`bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim examples/client_tcp -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
`bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west build -p always -b native_sim examples/modbus_gateway -- -DEXTRA_ZEPHYR_MODULES=/media/jeffry/jqb/GitHub/nanoMODBUS"`
Expected: All samples build cleanly with zero compiler warnings.

Run Twister test runner:
`bash -c "source /home/jeffry/.zephyr-venv/bin/activate && source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh && west twister -T tests/zephyr -p native_sim --inline-logs"`
Expected: 100% test pass rate across all suites.

- [ ] **Step 5: Commit**

```bash
git add examples/ tests/zephyr/
git commit -m "feat(zephyr): add multi-instance gateway, application samples, and Twister test suite"
```
