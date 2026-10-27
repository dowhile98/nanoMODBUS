# nanoMODBUS Zephyr RTOS Shell Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a comprehensive, deterministic, zero-allocation Zephyr RTOS Shell CLI interface (`nmbs`) for interactive Modbus client transactions (FC 01, 02, 03, 04, 05, 06, 15, 16) and server diagnostic monitoring over console interfaces (UART, Segger RTT, Telnet, USB CDC ACM).

**Architecture:** Implement an Adapter component in `zephyr/src/nanomodbus_z_shell.c` backed by a static instance registry table. Shell client commands serialize through the existing `client->lock` (`struct k_mutex`) to ensure safe concurrency with background application threads, while server diagnostics query atomic counters (`atomic_t`) lock-free. All console I/O strictly uses Zephyr Shell APIs (`shell_print()`, `shell_error()`, etc.) with zero heap allocations (`k_malloc`) and bounded stack buffers.

**Tech Stack:** Zephyr RTOS v3.5+/v4.x, C99/C11, Zephyr Shell subsystem (`CONFIG_SHELL`), Zephyr Dummy Shell backend (`CONFIG_SHELL_BACKEND_DUMMY`), Ztest, Kconfig, CMake 3.20+, west.

**Spec:** `docs/superpowers/specs/2026-10-25-zephyr-shell-integration-design.md`

---

## Global Constraints

- **Upstream Purity (Zero Zephyr Contamination in Core)**: `nanomodbus.c` and `nanomodbus.h` remain 100% pure ISO C99. Core files MUST NEVER include `<zephyr/...>`, RTOS primitives, or shell headers.
- **Strict Zephyr Logging & Shell Output (Zero `printk` / Zero `printf`)**: Console output MUST exclusively use Zephyr Shell API functions (`shell_print()`, `shell_error()`, `shell_warn()`, `shell_info()`, `shell_help()`). ZERO `printf` and ZERO `printk` anywhere in `zephyr/` or `samples/`. Module background logging uses Zephyr logging (`LOG_INF`, `LOG_DBG`, `LOG_ERR`, `LOG_WRN`) gated by `CONFIG_NANOMODBUS_LOG_LEVEL`.
- **Zero Dynamic Allocation Guarantee (Zero `malloc` / Zero `k_malloc`)**: Zero heap memory allocation. Client and server registries utilize bounded static tables sized at compile time via Kconfig (`CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS`, `CONFIG_NANOMODBUS_SHELL_MAX_SERVERS`). Stack buffers are strictly bounded by `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS`.
- **Deterministic Thread-Safety & Mutex Serialization**: Shell commands execute on the calling shell backend thread (`shell_uart`, `shell_rtt`, etc.). All client operations strictly acquire and release `client->lock` (`struct k_mutex`) to serialize with application background threads. Server diagnostics are non-blocking and lock-free using `atomic_t` counters.
- **Shell Execution Context & Timeouts**: Commands accept an optional timeout flag (`-t <ms>`, default client timeout). If the client lock cannot be acquired within the timeout, the command returns `-EBUSY` / `-ETIMEDOUT` with a clear message and does not stall or corrupt traffic.
- **Target Microcontroller & Board**: Hardware sample validation targets `nucleo_h743zi` (STM32H743ZI).

---

## Review Focus

The five key input classes and failure modes most likely to affect users:

1. **Registry Exhaustion & Duplicate Rejection**: Registering beyond `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS` (or `MAX_SERVERS`) returns `-ENOMEM`; registering duplicate names returns `-EEXIST`; unregistering non-existent instances returns `-ENOENT`. (Pinned to Task 2, Step 1).
2. **Client Lock Contention & Non-Disruptive Concurrency**: When an application thread holds `client->lock`, a shell read/write command times out cleanly after the specified `-t <ms>` timeout, outputting a descriptive error message without blocking indefinitely or crashing. (Pinned to Task 4, Step 1).
3. **Malformed & Overflow Radix Parsing**: Command inputs with malformed hex/oct/dec numbers (`0xZZ`, empty string, trailing alphabetic garbage) or out-of-range addresses/values are rejected with clear syntax errors before executing any bus transaction. (Pinned to Task 3, Step 1).
4. **Buffer Boundary > MAX_DATA_WORDS**: Requests specifying quantities exceeding `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS` (or bitfields exceeding `MAX_DATA_WORDS * 16`) are caught by the CLI validator and rejected with `-EINVAL`, preventing stack overflow. (Pinned to Task 3, Step 1).
5. **Modbus Exception Decoding**: When a Modbus slave returns an exception response (e.g. 0x02 `ILLEGAL_DATA_ADDRESS`), the shell decodes the raw `nmbs_error` via `nmbs_z_client_get_last_error()` and `nmbs_z_strerror()`, printing the exception name and code rather than a generic communication failure. (Pinned to Task 4, Step 1).

---

## Task Structure

```
Task 1: Kconfig, Build System & Public Interface Contract (nanomodbus_z_shell.h)
  ├── Task 2: Static Instance Registry (nanomodbus_z_shell.c registry functions)
  │     └── Task 3: Command Argument & Radix Parser (nanomodbus_z_shell.c input validators)
  │           └── Task 4: Modbus Client Interactive Commands (FC 01-06, 15, 16) with Dummy Shell
  │                 └── Task 5: Modbus Server Diagnostics & List Commands (nmbs list/status/reset-stats)
  │                       └── Task 6: Hardware Sample Integration & Cross-Compilation (nucleo_h743zi)
  │                             └── Task 7: AGENTS.md & Regression Verification
```

---

### Task 1: Kconfig, Build System & Public Interface Contract (`nanomodbus_z_shell.h`)

**Files:**
- Create: `zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h`
- Create: `zephyr/src/nanomodbus_z_shell.c` (initial stub)
- Create: `tests/zephyr/src/test_shell.c` (initial baseline test)
- Modify: `zephyr/Kconfig:225-290`
- Modify: `zephyr/CMakeLists.txt:20-25`
- Modify: `tests/zephyr/prj.conf:10-15`
- Modify: `tests/zephyr/CMakeLists.txt:5-14`

**Interfaces:**
- Consumes: `zephyr/include/zephyr/nanomodbus/nanomodbus_z.h`, `nanomodbus_z_serial.h`, `nanomodbus_z_tcp.h`
- Produces: Header `nanomodbus_z_shell.h` with:
  - `NMBS_Z_SHELL_NAME_MAX_LEN` (16)
  - `enum nmbs_z_server_state` (`NMBS_Z_SERVER_STATE_STOPPED`, `NMBS_Z_SERVER_STATE_RUNNING`, `NMBS_Z_SERVER_STATE_ERROR`)
  - `struct nmbs_z_server_stats` (`rx_frames`, `tx_frames`, `rx_errors`, `exceptions_sent`, `requests_handled`)
  - `struct nmbs_z_server_info` (`state`, `unit_id`, `port`, `stats`)
  - `typedef int (*nmbs_z_server_diag_cb)(void *server_ctx, struct nmbs_z_server_info *info, bool reset);`
  - Function prototypes:
    - `int nmbs_z_shell_register_client(const char *name, nmbs_z_client_t *client);`
    - `int nmbs_z_shell_unregister_client(const char *name);`
    - `int nmbs_z_shell_register_server(const char *name, void *server_ctx, nmbs_z_server_diag_cb diag_cb);`
    - `int nmbs_z_shell_unregister_server(const char *name);`
    - `int nmbs_z_shell_register_server_rtu(const char *name, nmbs_z_server_rtu_t *server);`
    - `int nmbs_z_shell_register_server_tcp(const char *name, nmbs_z_server_tcp_t *server);`

- [ ] **Step 1: Write the failing test**

Create `tests/zephyr/src/test_shell.c` asserting presence and baseline behavior of shell configuration symbols and interface declarations:
```c
/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>

ZTEST_SUITE(nanomodbus_shell, NULL, NULL, NULL, NULL, NULL);

ZTEST(nanomodbus_shell, test_shell_interface_and_kconfig_baseline)
{
    /* Verify Kconfig configuration bounds */
    zassert_equal(CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS, 4, "Default client limit mismatch");
    zassert_equal(CONFIG_NANOMODBUS_SHELL_MAX_SERVERS, 4, "Default server limit mismatch");
    zassert_equal(CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, 64, "Default data words limit mismatch");
    zassert_equal(NMBS_Z_SHELL_NAME_MAX_LEN, 16, "Name max len mismatch");

    /* Null pointer argument validation */
    zassert_equal(nmbs_z_shell_register_client(NULL, NULL), -EINVAL, "Expected -EINVAL on NULL client");
    zassert_equal(nmbs_z_shell_unregister_client(NULL), -EINVAL, "Expected -EINVAL on NULL name");
    zassert_equal(nmbs_z_shell_register_server(NULL, NULL, NULL), -EINVAL, "Expected -EINVAL on NULL server");
    zassert_equal(nmbs_z_shell_unregister_server(NULL), -EINVAL, "Expected -EINVAL on NULL name");
}
```

Add `src/test_shell.c` to `tests/zephyr/CMakeLists.txt`:
```cmake
target_sources(app PRIVATE
    ...
    src/test_shell.c
)
```

Add shell configs to `tests/zephyr/prj.conf`:
```properties
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_DUMMY=y
CONFIG_NANOMODBUS_SHELL=y
CONFIG_NANOMODBUS_SHELL_CLIENT=y
CONFIG_NANOMODBUS_SHELL_SERVER=y
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
```
Expected: Build failure due to missing `zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h` and undefined symbols `CONFIG_NANOMODBUS_SHELL*`.

- [ ] **Step 3: Implement Kconfig, CMakeLists.txt, public header, and stub implementation**

1. Create `zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h` with exact prototypes and data structures per Spec Section 4.1.
2. In `zephyr/Kconfig`, add `menuconfig NANOMODBUS_SHELL` with sub-options `CONFIG_NANOMODBUS_SHELL_CLIENT`, `CONFIG_NANOMODBUS_SHELL_SERVER`, `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS` (default 4), `CONFIG_NANOMODBUS_SHELL_MAX_SERVERS` (default 4), and `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS` (default 64) per Spec Section 3.1.
3. In `zephyr/CMakeLists.txt`, add:
```cmake
zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_SHELL src/nanomodbus_z_shell.c)
```
4. Create minimal stub `zephyr/src/nanomodbus_z_shell.c`:
```c
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>
#include <errno.h>

#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT
int nmbs_z_shell_register_client(const char *name, nmbs_z_client_t *client)
{
    if (!name || !client) return -EINVAL;
    return -ENOSYS;
}

int nmbs_z_shell_unregister_client(const char *name)
{
    if (!name) return -EINVAL;
    return -ENOSYS;
}
#endif

#ifdef CONFIG_NANOMODBUS_SHELL_SERVER
int nmbs_z_shell_register_server(const char *name, void *server_ctx, nmbs_z_server_diag_cb diag_cb)
{
    if (!name || !server_ctx || !diag_cb) return -EINVAL;
    return -ENOSYS;
}

int nmbs_z_shell_unregister_server(const char *name)
{
    if (!name) return -EINVAL;
    return -ENOSYS;
}

#ifdef CONFIG_NANOMODBUS_SERVER_RTU
int nmbs_z_shell_register_server_rtu(const char *name, nmbs_z_server_rtu_t *server)
{
    if (!name || !server) return -EINVAL;
    return -ENOSYS;
}
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
int nmbs_z_shell_register_server_tcp(const char *name, nmbs_z_server_tcp_t *server)
{
    if (!name || !server) return -EINVAL;
    return -ENOSYS;
}
#endif
#endif
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_interface_and_kconfig_baseline
```
Expected: PASS - `test_shell_interface_and_kconfig_baseline succeeded` with 0 failures.

- [ ] **Step 5: Commit**

```bash
git add zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h zephyr/src/nanomodbus_z_shell.c zephyr/Kconfig zephyr/CMakeLists.txt tests/zephyr/CMakeLists.txt tests/zephyr/prj.conf tests/zephyr/src/test_shell.c
git commit -m "feat(zephyr): add shell Kconfig options, build integration, and nanomodbus_z_shell.h contract"
```

---

### Task 2: Static Instance Registry (`nanomodbus_z_shell.c` registry functions)

**Files:**
- Modify: `zephyr/src/nanomodbus_z_shell.c`
- Modify: `tests/zephyr/src/test_shell.c`

**Interfaces:**
- Consumes: `nanomodbus_z_shell.h`, `nmbs_z_client_t`, `struct nmbs_z_server_info`
- Produces:
  - `int nmbs_z_shell_register_client(const char *name, nmbs_z_client_t *client);`
  - `int nmbs_z_shell_unregister_client(const char *name);`
  - `int nmbs_z_shell_register_server(const char *name, void *server_ctx, nmbs_z_server_diag_cb diag_cb);`
  - `int nmbs_z_shell_unregister_server(const char *name);`
  - Internal registry lookup helpers:
    - `struct client_entry *nmbs_z_shell_find_client(const char *name);`
    - `struct server_entry *nmbs_z_shell_find_server(const char *name);`
    - `struct client_entry *nmbs_z_shell_get_default_client(void);`
    - `struct server_entry *nmbs_z_shell_get_default_server(void);`

- [ ] **Step 1: Write the failing tests**

Add registry lifecycle tests to `tests/zephyr/src/test_shell.c`:
```c
ZTEST(nanomodbus_shell, test_shell_register_client_success)
{
    static nmbs_z_client_t dummy_client;
    zassert_equal(nmbs_z_shell_register_client("rtu0", &dummy_client), 0, "Registration failed");
    zassert_equal(nmbs_z_shell_unregister_client("rtu0"), 0, "Unregistration failed");
}

ZTEST(nanomodbus_shell, test_shell_register_client_duplicate)
{
    static nmbs_z_client_t dummy_client1;
    static nmbs_z_client_t dummy_client2;
    zassert_equal(nmbs_z_shell_register_client("dup", &dummy_client1), 0, "First registration failed");
    zassert_equal(nmbs_z_shell_register_client("dup", &dummy_client2), -EEXIST, "Expected -EEXIST for duplicate");
    nmbs_z_shell_unregister_client("dup");
}

ZTEST(nanomodbus_shell, test_shell_register_client_boundary_exhaustion)
{
    static nmbs_z_client_t clients[CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS + 1];
    char name[16];

    /* Register up to capacity */
    for (int i = 0; i < CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS; i++) {
        snprintf(name, sizeof(name), "c%d", i);
        zassert_equal(nmbs_z_shell_register_client(name, &clients[i]), 0, "Registration failed at %d", i);
    }

    /* Next one must fail with -ENOMEM */
    zassert_equal(nmbs_z_shell_register_client("overflow", &clients[CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS]),
                  -ENOMEM, "Expected -ENOMEM on registry full");

    /* Cleanup */
    for (int i = 0; i < CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS; i++) {
        snprintf(name, sizeof(name), "c%d", i);
        nmbs_z_shell_unregister_client(name);
    }
}

ZTEST(nanomodbus_shell, test_shell_unregister_client_not_found)
{
    zassert_equal(nmbs_z_shell_unregister_client("nonexistent"), -ENOENT, "Expected -ENOENT");
}

ZTEST(nanomodbus_shell, test_shell_register_server_lifecycle)
{
    int dummy_ctx = 1;
    zassert_equal(nmbs_z_shell_register_server("srv0", &dummy_ctx, (nmbs_z_server_diag_cb)0x1), 0, "Reg failed");
    zassert_equal(nmbs_z_shell_register_server("srv0", &dummy_ctx, (nmbs_z_server_diag_cb)0x1), -EEXIST, "Exp -EEXIST");
    zassert_equal(nmbs_z_shell_unregister_server("srv0"), 0, "Unreg failed");
    zassert_equal(nmbs_z_shell_unregister_server("srv0"), -ENOENT, "Exp -ENOENT");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_register_*
```
Expected: FAIL - functions return `-ENOSYS`.

- [ ] **Step 3: Implement static registry in `zephyr/src/nanomodbus_z_shell.c`**

1. Define static registry descriptors:
```c
struct client_entry {
    char name[NMBS_Z_SHELL_NAME_MAX_LEN];
    nmbs_z_client_t *client;
};

struct server_entry {
    char name[NMBS_Z_SHELL_NAME_MAX_LEN];
    void *ctx;
    nmbs_z_server_diag_cb diag_cb;
};

static struct client_entry s_clients[CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS];
static struct server_entry s_servers[CONFIG_NANOMODBUS_SHELL_MAX_SERVERS];
static size_t s_client_count;
static size_t s_server_count;
```
2. Implement validation: verify `name != NULL`, `strlen(name) > 0`, `strlen(name) < NMBS_Z_SHELL_NAME_MAX_LEN`.
3. Check for duplicates: iterate `s_clients` and `s_servers`, return `-EEXIST` if `strcmp(entry->name, name) == 0`.
4. Check bounds: if `count >= MAX`, return `-ENOMEM`.
5. Implement `nmbs_z_shell_unregister_client` and `nmbs_z_shell_unregister_server`: compact remaining array entries on removal, return `-ENOENT` if not found.
6. Implement lookup helpers: `nmbs_z_shell_find_client()`, `nmbs_z_shell_find_server()`, `nmbs_z_shell_get_default_client()` (returns pointer if `s_client_count == 1`, NULL otherwise), and `nmbs_z_shell_get_default_server()`.

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_register_*
```
Expected: PASS - all registry lifecycle and boundary tests pass cleanly.

- [ ] **Step 5: Commit**

```bash
git add zephyr/src/nanomodbus_z_shell.c tests/zephyr/src/test_shell.c
git commit -m "feat(zephyr): implement static instance registry for shell client and server instances"
```

---

### Task 3: Command Argument & Radix Parser (`nanomodbus_z_shell.c` input validators)

**Files:**
- Modify: `zephyr/src/nanomodbus_z_shell.c`
- Modify: `tests/zephyr/src/test_shell.c`

**Interfaces:**
- Consumes: CLI argument vector (`argc`, `argv`), Zephyr `shell_error()`
- Produces:
  - `struct nmbs_z_cli_options`:
    ```c
    struct nmbs_z_cli_options {
        const char *client_name;
        const char *server_name;
        uint8_t unit_id;
        bool unit_id_specified;
        int32_t timeout_ms;
        size_t positional_idx[8];
        size_t positional_count;
    };
    ```
  - `int nmbs_z_shell_parse_options(const struct shell *sh, size_t argc, char **argv, struct nmbs_z_cli_options *opts);`
  - `int nmbs_z_shell_parse_u16(const struct shell *sh, const char *str, const char *param_name, uint16_t *val_out);`
  - `int nmbs_z_shell_parse_bool(const struct shell *sh, const char *str, const char *param_name, bool *val_out);`
  - `int nmbs_z_shell_parse_quantity(const struct shell *sh, const char *str, uint16_t max_qty, uint16_t *qty_out);`

- [ ] **Step 1: Write the failing tests**

Add radix and option parser unit tests in `tests/zephyr/src/test_shell.c`:
```c
ZTEST(nanomodbus_shell, test_shell_parser_radix_valid)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    uint16_t val = 0;

    /* Decimal */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "1234", "test", &val), 0, "Decimal parse failed");
    zassert_equal(val, 1234, "Decimal value mismatch");

    /* Hexadecimal */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "0x04D2", "test", &val), 0, "Hex parse failed");
    zassert_equal(val, 1234, "Hex value mismatch");

    /* Hexadecimal boundary 0xFFFF */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "0xFFFF", "test", &val), 0, "Max hex parse failed");
    zassert_equal(val, 65535, "Max hex value mismatch");
}

ZTEST(nanomodbus_shell, test_shell_parser_malformed_radix)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    uint16_t val = 0;

    /* Invalid hex char */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "0xZZ", "addr", &val), -EINVAL, "Expected -EINVAL for 0xZZ");

    /* Empty string */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "", "addr", &val), -EINVAL, "Expected -EINVAL for empty");

    /* Trailing garbage */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "123abc", "addr", &val), -EINVAL, "Expected -EINVAL for trailing text");

    /* Numerical overflow (> 65535) */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "70000", "addr", &val), -ERANGE, "Expected -ERANGE on overflow");
}

ZTEST(nanomodbus_shell, test_shell_parser_quantity_boundary_overflow)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    uint16_t qty = 0;

    /* Zero quantity */
    zassert_equal(nmbs_z_shell_parse_quantity(sh, "0", CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty),
                  -EINVAL, "Zero quantity should fail");

    /* Valid quantity */
    zassert_equal(nmbs_z_shell_parse_quantity(sh, "64", CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty),
                  0, "Max quantity failed");
    zassert_equal(qty, 64, "Quantity mismatch");

    /* Out of bounds (> MAX_DATA_WORDS) */
    zassert_equal(nmbs_z_shell_parse_quantity(sh, "65", CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty),
                  -EINVAL, "Quantity > MAX_DATA_WORDS should fail");
}

ZTEST(nanomodbus_shell, test_shell_parser_boolean_variants)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    bool val = false;

    /* Truthy variants */
    const char *truthy[] = {"1", "on", "ON", "true", "TRUE", "high", "HIGH"};
    for (int i = 0; i < ARRAY_SIZE(truthy); i++) {
        zassert_equal(nmbs_z_shell_parse_bool(sh, truthy[i], "coil", &val), 0, "Truthy %s failed", truthy[i]);
        zassert_true(val, "Truthy %s not true", truthy[i]);
    }

    /* Falsy variants */
    const char *falsy[] = {"0", "off", "OFF", "false", "FALSE", "low", "LOW"};
    for (int i = 0; i < ARRAY_SIZE(falsy); i++) {
        zassert_equal(nmbs_z_shell_parse_bool(sh, falsy[i], "coil", &val), 0, "Falsy %s failed", falsy[i]);
        zassert_false(val, "Falsy %s not false", falsy[i]);
    }

    /* Invalid boolean */
    zassert_equal(nmbs_z_shell_parse_bool(sh, "maybe", "coil", &val), -EINVAL, "Invalid boolean accepted");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_parser_*
```
Expected: Build failure due to missing parser function declarations in `nanomodbus_z_shell.c`.

- [ ] **Step 3: Implement input and radix parser in `zephyr/src/nanomodbus_z_shell.c`**

1. Implement `nmbs_z_shell_parse_u16()`:
   - Use `strtoul(str, &endptr, 0)`.
   - Validate `*str != '\0'` and `*endptr == '\0'`.
   - Validate `parsed <= 0xFFFF`. If `> 0xFFFF`, `shell_error(sh, "%s overflow (>65535): '%s'", param_name, str); return -ERANGE;`.
   - On parse error, `shell_error(sh, "Invalid number format for %s: '%s'", param_name, str); return -EINVAL;`.
2. Implement `nmbs_z_shell_parse_quantity()`:
   - Call `nmbs_z_shell_parse_u16()`.
   - Verify `*qty_out >= 1 && *qty_out <= max_qty`.
   - On error, `shell_error(sh, "Quantity must be between 1 and %u (requested %u)", max_qty, *qty_out); return -EINVAL;`.
3. Implement `nmbs_z_shell_parse_bool()`:
   - Case-insensitive string comparison against `"1"`, `"on"`, `"true"`, `"high"` -> `true`.
   - Case-insensitive string comparison against `"0"`, `"off"`, `"false"`, `"low"` -> `false`.
   - Otherwise, `shell_error(sh, "Invalid boolean value for %s: '%s'. Expected on/off, true/false, 1/0, high/low", param_name, str); return -EINVAL;`.
4. Implement `nmbs_z_shell_parse_options()`:
   - Scan `argv[1..argc-1]`.
   - Arguments starting with `'-'`: inspect `-c`, `-s`, `-u`, `-t`.
   - Store positional argument indices into `opts->positional_idx` array.
   - Return `-EINVAL` on unknown option flag: `shell_error(sh, "Unknown option: %s", argv[i])`.

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_parser_*
```
Expected: PASS - all radix, boundary, boolean, and options parsing tests pass.

- [ ] **Step 5: Commit**

```bash
git add zephyr/src/nanomodbus_z_shell.c tests/zephyr/src/test_shell.c
git commit -m "feat(zephyr): add robust radix and argument validation helpers for shell commands"
```

---

### Task 4: Modbus Client Interactive Commands (FC 01, 02, 03, 04, 05, 06, 15, 16) with Dummy Shell tests

**Files:**
- Modify: `zephyr/src/nanomodbus_z_shell.c`
- Modify: `tests/zephyr/src/test_shell.c`

**Interfaces:**
- Consumes: Argument parser from Task 3, Client Registry from Task 2, `nmbs_z_client_*` APIs from `nanomodbus_z.h`.
- Produces: Zephyr shell subcommands under root `nmbs`:
  - `read-coils` (FC 01)
  - `read-discrete` (FC 02)
  - `read-holding` (FC 03)
  - `read-input` (FC 04)
  - `write-coil` (FC 05)
  - `write-register` (FC 06)
  - `write-coils` (FC 15)
  - `write-registers` (FC 16)
  - Output table formatters: `format_registers_table()` and `format_coils_bitfield()`.
  - Error and exception decoder using `nmbs_z_client_get_last_error()` and `nmbs_z_strerror()`.

- [ ] **Step 1: Write the failing tests**

In `tests/zephyr/src/test_shell.c`, set up a loopback mock client with simulated registers and coils, and write tests for each client command:
```c
static uint16_t s_mock_holding[16] = {0x03E8, 0x07D0, 0x0000, 0xFFFF};
static uint8_t s_mock_coils = 0x4D; /* 0b01001101 */
static nmbs_z_client_t s_test_client;
static nmbs_platform_conf s_test_platform;

/* Mock read/write transport functions returning mock data */
/* ... */

ZTEST(nanomodbus_shell, test_shell_cmd_read_holding_fc03)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-holding 0 4");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "1000 (0x03E8)"), "Missing reg 0 in output: %s", out);
    zassert_not_null(strstr(out, "2000 (0x07D0)"), "Missing reg 1 in output: %s", out);
    zassert_not_null(strstr(out, "65535 (0xFFFF)"), "Missing reg 3 in output: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_read_coils_fc01)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-coils 0 8");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "(0x4D)"), "Missing coil bitfield byte: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_write_reg_fc06)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs write-register 0x0010 1234");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "OK: Wrote register at address 0x0010"), "Missing OK message: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_modbus_exception_decoding)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    /* Configure mock transport to respond with Modbus Exception 0x02 */
    s_mock_force_exception = 0x02;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-holding 0x5000 1");
    zassert_not_equal(ret, 0, "Expected non-zero return code on exception");

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Modbus Exception:"), "Missing exception header: %s", out);
    zassert_not_null(strstr(out, "(Code 0x02)"), "Missing exception code 0x02: %s", out);
    s_mock_force_exception = 0;
}

ZTEST(nanomodbus_shell, test_shell_cmd_lock_contention_timeout)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    /* Deliberately hold client mutex from test thread */
    k_mutex_lock(&s_test_client.lock, K_FOREVER);

    /* Run command with a short 50 ms timeout */
    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-holding -t 50 0 1");
    zassert_not_equal(ret, 0, "Expected failure on lock contention");

    k_mutex_unlock(&s_test_client.lock);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Client busy: Lock timeout expired"), "Missing timeout msg: %s", out);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_cmd_*
```
Expected: FAIL - commands not recognized (`nmbs: command not found`).

- [ ] **Step 3: Implement client commands and formatters in `zephyr/src/nanomodbus_z_shell.c`**

1. Implement table formatting helpers:
   - `format_registers_table(const struct shell *sh, uint16_t start_addr, const uint16_t *regs, uint16_t count)`:
     Prints 4 registers per line in format `[0x%04X / %05u]: %6u (0x%04X) ...`.
   - `format_coils_bitfield(const struct shell *sh, uint16_t start_addr, const nmbs_bitfield coils, uint16_t count)`:
     Prints 8 bits per line in format `[0x%04X / %05u]: 1 0 1 1 0 0 1 0 (0x%02X)`.
2. Implement error reporter helper:
   ```c
   static int report_client_error(const struct shell *sh, nmbs_z_client_t *client, int ret)
   {
       nmbs_error raw = nmbs_z_client_get_last_error(client);
       if (raw == NMBS_ERROR_TIMEOUT && ret == -EBUSY) {
           shell_error(sh, "Client busy: Lock timeout expired");
       } else if (nmbs_error_is_exception(raw)) {
           shell_error(sh, "Modbus Exception: %s (Code 0x%02X)", nmbs_z_strerror(raw), (uint8_t)raw);
       } else {
           shell_error(sh, "Communication Error: %s (errno %d)", nmbs_z_strerror(raw), ret);
       }
       return ret;
   }
   ```
3. Implement command handlers:
   - `cmd_nmbs_read_holding()`: parses args, resolves client, acquires target address, invokes `nmbs_z_client_read_holding_registers()`, formats table.
   - `cmd_nmbs_read_input()`: invokes `nmbs_z_client_read_input_registers()`, formats table.
   - `cmd_nmbs_read_coils()`: invokes `nmbs_z_client_read_coils()`, formats bitfield.
   - `cmd_nmbs_read_discrete()`: invokes `nmbs_z_client_read_discrete_inputs()`, formats bitfield.
   - `cmd_nmbs_write_coil()`: parses boolean, invokes `nmbs_z_client_write_single_coil()`.
   - `cmd_nmbs_write_register()`: parses u16, invokes `nmbs_z_client_write_single_register()`.
   - `cmd_nmbs_write_coils()`: gathers coil booleans into bitfield, invokes `nmbs_z_client_write_multiple_coils()`.
   - `cmd_nmbs_write_registers()`: gathers register u16 words, invokes `nmbs_z_client_write_multiple_registers()`.
4. Register root command and subcommands using Zephyr macros:
   ```c
   SHELL_STATIC_SUBCMD_SET_CREATE(sub_nmbs,
       SHELL_CMD_ARG(read-coils, NULL, "Read coils (FC 01)", cmd_nmbs_read_coils, 3, 6),
       SHELL_CMD_ARG(read-discrete, NULL, "Read discrete inputs (FC 02)", cmd_nmbs_read_discrete, 3, 6),
       SHELL_CMD_ARG(read-holding, NULL, "Read holding registers (FC 03)", cmd_nmbs_read_holding, 3, 6),
       SHELL_CMD_ARG(read-input, NULL, "Read input registers (FC 04)", cmd_nmbs_read_input, 3, 6),
       SHELL_CMD_ARG(write-coil, NULL, "Write single coil (FC 05)", cmd_nmbs_write_coil, 3, 6),
       SHELL_CMD_ARG(write-register, NULL, "Write single register (FC 06)", cmd_nmbs_write_register, 3, 6),
       SHELL_CMD_ARG(write-coils, NULL, "Write multiple coils (FC 15)", cmd_nmbs_write_coils, 4, 128),
       SHELL_CMD_ARG(write-registers, NULL, "Write multiple registers (FC 16)", cmd_nmbs_write_registers, 4, 128),
       SHELL_SUBCMD_SET_END
   );
   SHELL_CMD_REGISTER(nmbs, &sub_nmbs, "nanoMODBUS commands", NULL);
   ```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_cmd_*
```
Expected: PASS - all FC 01-06, 15, 16 tests, formatting tests, exception decoding, and lock contention timeout tests pass.

- [ ] **Step 5: Commit**

```bash
git add zephyr/src/nanomodbus_z_shell.c tests/zephyr/src/test_shell.c
git commit -m "feat(zephyr): add interactive Modbus client shell subcommands with table formatting and exception decoding"
```

---

### Task 5: Modbus Server Diagnostics & List Commands (`nmbs list`, `nmbs server status`, `nmbs server reset-stats`)

**Files:**
- Modify: `zephyr/src/nanomodbus_z_shell.c`
- Modify: `tests/zephyr/src/test_shell.c`

**Interfaces:**
- Consumes: Server Registry from Task 2, `struct nmbs_z_server_info`, `struct nmbs_z_server_stats`
- Produces:
  - Subcommand `nmbs list`
  - Subcommand `nmbs status`
  - Subcommand `nmbs reset-stats`
  - Server registration helpers:
    - `int nmbs_z_shell_register_server_rtu(const char *name, nmbs_z_server_rtu_t *server);`
    - `int nmbs_z_shell_register_server_tcp(const char *name, nmbs_z_server_tcp_t *server);`

- [ ] **Step 1: Write the failing tests**

Add list and server diagnostic tests in `tests/zephyr/src/test_shell.c`:
```c
static int mock_server_diag_cb(void *ctx, struct nmbs_z_server_info *info, bool reset)
{
    static struct nmbs_z_server_stats stats = {
        .rx_frames = 100,
        .tx_frames = 98,
        .rx_errors = 2,
        .exceptions_sent = 1,
        .requests_handled = 97
    };

    info->state = NMBS_Z_SERVER_STATE_RUNNING;
    info->unit_id = 1;
    info->port = 0;
    info->stats = stats;

    if (reset) {
        memset(&stats, 0, sizeof(stats));
    }
    return 0;
}

ZTEST(nanomodbus_shell, test_shell_cmd_list)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs list");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "nanoMODBUS Registered Instances:"), "Missing header: %s", out);
    zassert_not_null(strstr(out, "Clients:"), "Missing clients section: %s", out);
    zassert_not_null(strstr(out, "Servers:"), "Missing servers section: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_server_status)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    nmbs_z_shell_register_server("srv_test", (void *)0x1234, mock_server_diag_cb);

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs status -s srv_test");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Frames Received:    100"), "Missing rx_frames: %s", out);
    zassert_not_null(strstr(out, "Checksum Errors:    2"), "Missing rx_errors: %s", out);
    zassert_not_null(strstr(out, "Operational State:  RUNNING"), "Missing state: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_server_reset_stats)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs reset-stats -s srv_test");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "reset to 0"), "Missing reset confirmation: %s", out);

    /* Verify subsequent status displays 0 */
    shell_backend_dummy_clear_output(sh);
    shell_execute_cmd(sh, "nmbs status -s srv_test");
    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Frames Received:    0"), "Stats not reset: %s", out);

    nmbs_z_shell_unregister_server("srv_test");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::test_shell_cmd_server_*
```
Expected: FAIL - subcommands `list`, `status`, `reset-stats` not found.

- [ ] **Step 3: Implement list, status, reset-stats subcommands and server helpers in `zephyr/src/nanomodbus_z_shell.c`**

1. Implement `cmd_nmbs_list()`:
   - Prints all registered clients: index, name, (default indicator), unit ID, and configured timeouts.
   - Prints all registered servers: index, name, (default indicator), type (RTU/TCP/Custom), unit ID or port, and current state.
2. Implement `cmd_nmbs_status()`:
   - Resolves target server via `-s <server>` or default single server.
   - Invokes server's `diag_cb(ctx, &info, false)`.
   - Formats and displays operational state, Unit ID/Port, received frames, transmitted frames, checksum errors, exceptions sent, and handled requests per Spec Section 5.2.10.
3. Implement `cmd_nmbs_reset_stats()`:
   - Resolves target server.
   - Invokes `diag_cb(ctx, &info, true)`.
   - Confirms reset with `shell_print(sh, "OK: Diagnostic counters for server '%s' reset to 0.", name);`.
4. Implement standard helpers:
   - `nmbs_z_shell_register_server_rtu()`: extracts `server->server_address`, inspects `atomic_get(&server->running)`, registers standard diagnostic callback.
   - `nmbs_z_shell_register_server_tcp()`: extracts `server->port`, inspects `atomic_get(&server->running)`, registers standard diagnostic callback.
5. Add `list`, `status`, `reset-stats` to `sub_nmbs` static command table.

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
west build -p always -b native_sim tests/zephyr -d build_ztest -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_shell::*
```
Expected: PASS - all tests in `nanomodbus_shell` suite pass with 100% success.

- [ ] **Step 5: Commit**

```bash
git add zephyr/src/nanomodbus_z_shell.c tests/zephyr/src/test_shell.c
git commit -m "feat(zephyr): add nmbs list, status, and reset-stats diagnostic subcommands and server helpers"
```

---

### Task 6: Hardware Sample Integration & Cross-Compilation (`nucleo_h743zi`)

**Files:**
- Modify: `samples/client_rtu/prj.conf`
- Modify: `samples/client_rtu/src/main.c:108-125`
- Modify: `samples/server_rtu/prj.conf`
- Modify: `samples/server_rtu/src/main.c:90-110`

**Interfaces:**
- Consumes: `nanomodbus_z_shell.h`
- Produces: Interactive shell support compiled into `samples/client_rtu` and `samples/server_rtu` for target board `nucleo_h743zi`.

- [ ] **Step 1: Write sample registration updates**

In `samples/client_rtu/src/main.c`:
1. Include `<zephyr/nanomodbus/nanomodbus_z_shell.h>` gated by `CONFIG_NANOMODBUS_SHELL`.
2. After `nmbs_z_client_init(&client, &platform)`:
```c
#ifdef CONFIG_NANOMODBUS_SHELL
    ret = nmbs_z_shell_register_client("rtu0", &client);
    if (ret != 0) {
        LOG_WRN("Failed to register client with shell: %d", ret);
    } else {
        LOG_INF("Registered client 'rtu0' with shell CLI");
    }
#endif
```

In `samples/server_rtu/src/main.c`:
1. Include `<zephyr/nanomodbus/nanomodbus_z_shell.h>` gated by `CONFIG_NANOMODBUS_SHELL`.
2. After `nmbs_z_server_rtu_init(&server, ...)`:
```c
#ifdef CONFIG_NANOMODBUS_SHELL
    ret = nmbs_z_shell_register_server_rtu("srv0", &server);
    if (ret != 0) {
        LOG_WRN("Failed to register server with shell: %d", ret);
    } else {
        LOG_INF("Registered server 'srv0' with shell CLI");
    }
#endif
```

- [ ] **Step 2: Enable shell in sample project configs**

Add shell configuration options to `samples/client_rtu/prj.conf` and `samples/server_rtu/prj.conf`:
```properties
CONFIG_SHELL=y
CONFIG_SHELL_BACKENDS=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_NANOMODBUS_SHELL=y
```

- [ ] **Step 3: Cross-compile `samples/client_rtu` for `nucleo_h743zi`**

Run:
```bash
west build -p always -b nucleo_h743zi samples/client_rtu -d build_client_rtu -- -DEXTRA_ZEPHYR_MODULES=$PWD
```
Expected: Build succeeds with 0 errors and 0 warnings. Binary `build_client_rtu/zephyr/zephyr.elf` generated.

- [ ] **Step 4: Cross-compile `samples/server_rtu` for `nucleo_h743zi`**

Run:
```bash
west build -p always -b nucleo_h743zi samples/server_rtu -d build_server_rtu -- -DEXTRA_ZEPHYR_MODULES=$PWD
```
Expected: Build succeeds with 0 errors and 0 warnings. Binary `build_server_rtu/zephyr/zephyr.elf` generated.

- [ ] **Step 5: Commit**

```bash
git add samples/client_rtu/prj.conf samples/client_rtu/src/main.c samples/server_rtu/prj.conf samples/server_rtu/src/main.c
git commit -m "sample: integrate nanoMODBUS shell commands into client_rtu and server_rtu samples"
```

---

### Task 7: AGENTS.md & Regression Verification

**Files:**
- Modify: `AGENTS.md`

**Interfaces:**
- Consumes: All completed shell components, samples, and tests.
- Produces: Updated repository documentation and clean test report across host and target.

- [ ] **Step 1: Update `AGENTS.md` documentation**

Document:
1. Shell integration architecture: `nanomodbus_z_shell.c` / `nanomodbus_z_shell.h`.
2. New Kconfig symbols: `CONFIG_NANOMODBUS_SHELL`, `CONFIG_NANOMODBUS_SHELL_CLIENT`, `CONFIG_NANOMODBUS_SHELL_SERVER`, bounds.
3. New registered Ztest suite: `nanomodbus_shell` in `tests/zephyr/src/test_shell.c`.
4. CLI usage reference and sample command examples (`nmbs read-holding`, `nmbs write-register`, `nmbs list`, `nmbs status`).

- [ ] **Step 2: Run complete Zephyr test suite via Twister**

Run:
```bash
west twister -T tests/zephyr -p native_sim --extra-args ZEPHYR_MODULES=$PWD
```
Expected: All test suites pass (including `nanomodbus_shell`), 0 failures, 0 errors.

- [ ] **Step 3: Run Host CMake and CTest regression suite**

Run:
```bash
cmake -S . -B build_host -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host -j
ctest --test-dir build_host --output-on-failure
```
Expected: All 18+ upstream host tests pass with 100% success.

- [ ] **Step 4: Verify formatting and repository cleanliness**

Run:
```bash
git status
```
Expected: Clean working tree; only intended files modified/created.

- [ ] **Step 5: Commit**

```bash
git add -f AGENTS.md
git commit -m "docs: document nanoMODBUS shell integration and test suite in AGENTS.md"
```

---

## Plan Self-Review Checklist

- [x] **1. Spec Coverage**: Every requirement from `docs/superpowers/specs/2026-10-25-zephyr-shell-integration-design.md` is mapped to a concrete task:
  - Registry Architecture & Public Interface -> Task 1, Task 2
  - Kconfig Options & CMake Integration -> Task 1
  - CLI Radix & Argument Validation -> Task 3
  - Client Commands (FC 01, 02, 03, 04, 05, 06, 15, 16) & Output Formatting -> Task 4
  - Concurrency Lock Contention & Exception Decoding -> Task 4
  - Server Status, Reset-Stats & List Commands -> Task 5
  - Target Cross-Compilation (`nucleo_h743zi`) -> Task 6
  - Twister & Host Test Verification -> Task 7
- [x] **2. Step Scan**: Every step defines an exact checkable action with exact file paths, signatures, test names, shell commands, and git commits. No vague lines or transcripts.
- [x] **3. Type Consistency**: All types (`nmbs_z_client_t`, `nmbs_z_server_rtu_t`, `nmbs_z_server_tcp_t`, `struct nmbs_z_server_info`, `struct nmbs_z_server_stats`, `enum nmbs_z_server_state`) match across tasks and header contracts.
- [x] **4. Review Focus Alignment**: All 5 review focus items (Registry exhaustion, Client lock contention, Malformed radix parsing, Buffer boundary > MAX_DATA_WORDS, Modbus exception decoding) are explicitly listed in the header and exercised by dedicated automated tests in Tasks 2, 3, and 4.
- [x] **5. Proportion**: The plan is focused, modular, and directly actionable for an implementing developer.
