# nanoMODBUS Zephyr RTOS Shell Integration Specification

| Metadata | Details |
| :--- | :--- |
| **Document ID** | SPEC-NMBS-ZEPHYR-SHELL-001 |
| **Title** | nanoMODBUS Zephyr RTOS Shell Integration Specification |
| **Author** | Principal Embedded Software Architect |
| **Status** | Approved / Architecture Baseline |
| **Date** | 2026-10-25 |
| **Target System** | Zephyr RTOS v3.5+ / v4.x, C99/C11 |
| **Upstream Library** | nanoMODBUS v2.x |
| **Classification** | Architectural / Integration Specification |

---

## 1. Executive Summary & Architectural Goals

### 1.1 Context & Motivation
Field commissioning, hardware-in-the-loop (HIL) automated diagnostics, and interactive development of Modbus networks require a responsive, deterministic Command Line Interface (CLI). In embedded systems built with Zephyr RTOS, operators and firmware engineers frequently need to inspect live Modbus server registers, poll slave instrumentation over RS-485, override actuator coils, and verify communication error rates directly via serial UART consoles, Segger RTT, Telnet, or USB CDC ACM.

While nanoMODBUS provides a pure C99 protocol core and the Zephyr module provides thread-safe clients (`nmbs_z_client_t`) and servers (`nmbs_z_server_rtu_t`, `nmbs_z_server_tcp_t`), there is currently no standard CLI mechanism to interact with these instances. Applications are forced to write bespoke shell commands, leading to code duplication, race conditions with background polling loops, and violations of system concurrency invariants.

This specification defines the formal architecture for the **nanoMODBUS Zephyr RTOS Shell Integration**. It introduces the `nmbs` root command and a full suite of client diagnostic and server status subcommands, implemented via a **Static Instance Registry** architecture.

### 1.2 The Golden Architectural Invariants

Every component designed within this specification MUST adhere strictly to the following five golden invariants:

1. **Upstream Purity (Zero Zephyr Contamination in Core)**:
   - `nanomodbus.c` and `nanomodbus.h` remain 100% pure ISO C99.
   - Core files MUST NEVER include `<zephyr/...>`, RTOS primitives, or shell headers.
   - All shell and CLI logic belongs exclusively to the Zephyr adapter layer under `zephyr/src/` and `zephyr/include/zephyr/nanomodbus/`.

2. **Strict Zephyr Logging & Shell Output (Zero `printk` / Zero `printf`)**:
   - Console command output MUST exclusively use Zephyr Shell API functions: `shell_print()`, `shell_error()`, `shell_warn()`, `shell_info()`, and `shell_help()`.
   - Under no circumstances shall `printf()`, `printk()`, or direct console driver writes be used.
   - Background module events utilize Zephyr logging (`LOG_INF`, `LOG_DBG`, `LOG_ERR`, `LOG_WRN`) gated by `CONFIG_NANOMODBUS_LOG_LEVEL`.

3. **Zero Dynamic Allocation Guarantee (Zero `malloc` / Zero `k_malloc`)**:
   - Zero heap memory allocation. All client and server registries utilize bounded static tables sized at compile time via Kconfig (`CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS`, `CONFIG_NANOMODBUS_SHELL_MAX_SERVERS`).
   - Local command execution buffers (PDU frames, register arrays, bitfields) are statically sized and bounded on the shell thread stack by `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS`.

4. **Deterministic Thread-Safety & Non-Disruptive Concurrency**:
   - Shell command execution occurs on the dedicated thread of the active shell backend (e.g. `shell_uart`, `shell_rtt`, `shell_telnet`).
   - All client transactions serialize strictly through the existing mutual exclusion primitive (`client->lock`, `struct k_mutex`) defined inside `nmbs_z_client_t`.
   - Active application threads sharing the same client instance are protected from race conditions, frame interleaving, and corruption.
   - Server metrics and diagnostic retrieval are non-blocking and lock-free, utilizing atomic counters (`atomic_t`) to prevent any jitter in time-critical RTU t3.5 character silence timers or interrupt handlers.

5. **Multi-Instance Support with Intuitive Default Fallback**:
   - Supports multi-bus gateways and heterogeneous topologies where multiple RTU and TCP instances coexist.
   - Commands allow explicit instance targeting via the `-c <client>` or `-s <server>` option flag.
   - For single-client or single-server systems (the dominant 90% embedded use case), the shell automatically resolves the single registered instance as the default, eliminating repetitive command typing.

---

### 1.3 Architectural Overview Diagram

```
+==================================================================================================+
|                                    OPERATOR / CONSOLE TERMINAL                                   |
|                          (Serial UART Console, Segger RTT, Telnet, USB CDC ACM)                  |
+================================================+=================================================+
                                                 | User CLI Input: "nmbs read-holding 0x1000 4"
                                                 v
+==================================================================================================+
|                                   ZEPHYR SHELL SUBSYSTEM                                         |
|  +--------------------------------------------------------------------------------------------+  |
|  | Shell Backend Thread (shell_uart / shell_rtt)                                              |  |
|  | - Lexical tokenization, command dispatching, autocompletion, escape code handling          |  |
|  +---------------------------------------------+----------------------------------------------+  |
+================================================|=================================================+
                                                 | Dispatches to root handler: 'nmbs'
                                                 v
+==================================================================================================+
|                        NANOMODBUS ZEPHYR SHELL ADAPTER (nanomodbus_z_shell.c)                    |
|                                                                                                  |
|  +-----------------------------+  +-------------------------------+  +------------------------+  |
|  | Argument & Radix Parser     |  | Static Registry Tables        |  | Table & Bit Formatter  |  |
|  | - dec/hex (0x) validation   |  | - s_clients[MAX_CLIENTS]      |  | - Register hex/dec grid|  |
|  | - Flag parser (-c, -u, -t)  |  | - s_servers[MAX_SERVERS]      |  | - Error string mapper  |  |
|  +--------------+--------------+  +---------------+---------------+  +-----------+------------+  |
|                 |                                 |                              ^               |
|                 +----------------+----------------+                              |               |
|                                  | Resolves instance pointer                     |               |
+==================================|===============================================|===============+
                                   |                                               |
         +-------------------------+-------------------------+                     |
         | Client Operation                                  | Server Diagnostics  | Formats
         v                                                   v                     | output
+==================================+       +==================================+    |
|   nmbs_z_client_t Instance       |       |   nmbs_z_server_t Instance       |    |
|                                  |       |   (RTU workqueue or TCP thread)  |    |
|  +----------------------------+  |       |                                  |    |
|  | k_mutex lock               |  |       |  +----------------------------+  |    |
|  | (Serializes Shell + Apps)  |  |       |  | Atomic Diagnostic Counters |  |    |
|  +-------------+--------------+  |       |  | (rx_frames, errs, etc.)    |--+----+
|                | Lock acquired   |       +--+--------------+-------------+--+
|                v                 |                         |
|  +----------------------------+  |                         |
|  | nmbs_z_client_* API        |  |                         |
|  +-------------+--------------+  |                         |
+================|=================+                         |
                 |                                           |
                 v nmbs_platform_conf                        v
+==================================================================================================+
|                               PURE C99 DOMAIN CORE (nanomodbus.c)                                |
|  - Frame serialization, CRC-16 calculation, Exception PDU parsing, State validation              |
+================================================+=================================================+
                                                 |
                                                 v Transport callbacks
+==================================================================================================+
|                             ZEPHYR TRANSPORT HARDWARE ADAPTERS                                   |
|  +--------------------------------------------+  +--------------------------------------------+  |
|  | nanomodbus_z_transport_serial              |  | nanomodbus_z_transport_tcp                 |  |
|  | (UART IRQ/ASYNC, RS-485 DE GPIO, Timers)   |  | (Zephyr BSD Sockets, k_poll)               |  |
|  +---------------------+----------------------+  +---------------------+----------------------+  |
+========================|================================================|========================+
                         v                                                v
            [ Physical RS-485 Bus ]                            [ Ethernet / IP Network ]
```

---

### 1.4 Design Approach Comparison & Rationale

| Evaluation Criterion | Approach 1: Static Instance Registry (Selected) | Approach 2: Direct Driver / Devicetree Binding | Approach 3: Ad-hoc Application Shell Handlers |
| :--- | :--- | :--- | :--- |
| **Architectural Separation** | **Clean**: Decouples transport & configuration from shell logic. Zero Devicetree coupling. | **Poor**: Forces shell to know hardware pins, clocks, and baud rates; duplicates init code. | **Violated**: Each application invents its own shell commands and formatting rules. |
| **Concurrency Safety** | **Deterministic**: Direct reuse of `client->lock` prevents collisions with application threads. | **Fragile**: Bypasses existing application client state, creating hardware access collisions. | **Inconsistent**: Applications frequently omit mutex locking or timeout handling. |
| **Multi-Instance Support** | **Native**: Supports up to `MAX_CLIENTS` and `MAX_SERVERS` with named identifiers and default fallback. | **Complex**: Requires iterating Devicetree aliases or creating synthetic shell subcommands. | **Manual**: Application must hand-craft dispatchers for every added instance. |
| **Dynamic Memory Usage** | **Zero**: Completely bounded static memory; stack allocations bounded by Kconfig. | **Zero**: Static, but higher flash overhead due to Devicetree metadata. | **Variable**: Often relies on unbounded local arrays or `malloc`. |
| **Flash & RAM Footprint** | **Minimal**: ~2.8 KB ROM, <200 bytes static RAM for registry structures. | **Moderate**: ~4.5 KB ROM due to peripheral reconfiguration logic. | **High**: Duplicated across every project and sample. |

**Rationale for Approach 1 Selection**:
Approach 1 preserves the Ports and Adapters architecture. The shell component acts strictly as a user-facing Adapter operating over initialized Domain/Service instances (`nmbs_z_client_t`). It requires zero knowledge of Devicetree nodes or hardware registers, perfectly preserving the golden invariant of component classification.

---

## 2. Directory Structure & Component Classification

### 2.1 Component Taxonomy & Mapping

| Component Classification | File Path | Responsibilities & Invariants |
| :--- | :--- | :--- |
| **Interface** (`app/ports/include/`, `zephyr/include/`) | `zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h` | Pure C contract. Declares instance registration prototypes, server diagnostic statistics structures, and diagnostic callbacks. Zero vendor headers. |
| **Adapter / CLI Handler** (`src/infrastructure/adapters/`, `zephyr/src/`) | `zephyr/src/nanomodbus_z_shell.c` | Zephyr Shell integration. Registers the `nmbs` root command tree, parses command line tokens, validates numerical radices, executes Modbus transactions, and formats console output. |
| **Build System / Kconfig** (`zephyr/`) | `zephyr/Kconfig`<br>`zephyr/CMakeLists.txt` | Defines compile-time configuration tree, instance bounds, and source conditional compilation. |
| **Verification / Test** (`tests/zephyr/`) | `tests/zephyr/src/test_shell.c` | Automated unit and integration test suite executing via Zephyr Dummy Shell backend (`CONFIG_SHELL_BACKEND_DUMMY=y`). |

### 2.2 Repository Tree Structure

```
nanoMODBUS/
├── nanomodbus.c                                  # [Domain] Pure C99 core protocol engine
├── nanomodbus.h                                  # [Domain] Core protocol header
├── zephyr/
│   ├── CMakeLists.txt                            # Module build configuration (updated for shell)
│   ├── Kconfig                                   # Module feature tree (updated for shell)
│   ├── include/
│   │   └── zephyr/
│   │       └── nanomodbus/
│   │           ├── nanomodbus_z.h                # [Interface] Core Zephyr types & client APIs
│   │           ├── nanomodbus_z_serial.h         # [Interface] Serial RTU transport & server
│   │           ├── nanomodbus_z_tcp.h            # [Interface] TCP transport & server
│   │           └── nanomodbus_z_shell.h          # [Interface] NEW: Shell registration contracts
│   └── src/
│       ├── nanomodbus_z_client.c                 # [Service] Mutex-wrapped client implementation
│       ├── nanomodbus_z_common.c                 # [Service] Error and errno conversion helpers
│       ├── nanomodbus_z_server_rtu.c             # [Service] Event-driven RTU server engine
│       ├── nanomodbus_z_server_tcp.c             # [Active Object] Threaded TCP server engine
│       ├── nanomodbus_z_transport_serial.c       # [Driver/Adapter] UART & RS-485 DE driver
│       ├── nanomodbus_z_transport_tcp.c          # [Adapter] BSD Sockets TCP adapter
│       └── nanomodbus_z_shell.c                  # [Adapter] NEW: Shell CLI handlers & registry
└── tests/
    └── zephyr/
        ├── CMakeLists.txt                        # Test build definition (links test_shell.c)
        ├── prj.conf                              # Test configuration
        └── src/
            └── test_shell.c                      # [Verification] NEW: Ztest shell command suite
```

### 2.3 Dependency Flow & Coupling Analysis

```
       +---------------------------------------------+
       |           Zephyr Shell Subsystem            |
       +----------------------+----------------------+
                              | invokes handler
                              v
       +---------------------------------------------+
       |   nanomodbus_z_shell.c [CLI Adapter]        |
       +----------------------+----------------------+
                              | includes
                              v
       +---------------------------------------------+
       |   nanomodbus_z_shell.h [Public Interface]   |
       +----------------------+----------------------+
                              | references
                              v
       +---------------------------------------------+
       |     nanomodbus_z.h [Client & Server API]    |
       +----------------------+----------------------+
                              | wraps
                              v
       +---------------------------------------------+
       |    nanomodbus.h / nanomodbus.c [Core C99]   |
       +---------------------------------------------+
```

- **Strict Downward Dependency**: Higher-level modules depend only on abstract interfaces below them.
- **Zero Upward Coupling**: `nanomodbus.c` and `nanomodbus_z.c` have zero knowledge of `nanomodbus_z_shell.h`.
- **Zero Vendor Contamination**: No hardware HAL headers (`stm32*.h`, `nrf*.h`) appear anywhere in the shell integration.

---

## 3. Kconfig Specifications

### 3.1 Kconfig Hierarchy & Symbol Definitions

The shell configuration options are located in `zephyr/Kconfig` within the `NANOMODBUS` menu:

```kconfig
# zephyr/Kconfig snippet

menuconfig NANOMODBUS_SHELL
	bool "nanoMODBUS Shell commands"
	depends on SHELL && NANOMODBUS
	default y
	help
	  Enable interactive Zephyr Shell CLI commands for nanoMODBUS. Provides
	  diagnostic inspection, bus monitoring, and manual Modbus read/write
	  operations over console interfaces.

if NANOMODBUS_SHELL

config NANOMODBUS_SHELL_CLIENT
	bool "nanoMODBUS Shell Client commands"
	depends on NANOMODBUS_CLIENT
	default y
	help
	  Enable Modbus client commands in the shell:
	  - read-coils (FC 01)
	  - read-discrete (FC 02)
	  - read-holding (FC 03)
	  - read-input (FC 04)
	  - write-coil (FC 05)
	  - write-register (FC 06)
	  - write-coils (FC 15)
	  - write-registers (FC 16)

config NANOMODBUS_SHELL_SERVER
	bool "nanoMODBUS Shell Server diagnostics"
	depends on NANOMODBUS_SERVER
	default y
	help
	  Enable Modbus server diagnostic commands in the shell:
	  - status
	  - reset-stats

config NANOMODBUS_SHELL_MAX_CLIENTS
	int "Maximum registered client instances"
	depends on NANOMODBUS_SHELL_CLIENT
	default 4
	range 1 8
	help
	  Maximum number of named client instances that can be registered
	  in the static shell registry.

config NANOMODBUS_SHELL_MAX_SERVERS
	int "Maximum registered server instances"
	depends on NANOMODBUS_SHELL_SERVER
	default 4
	range 1 8
	help
	  Maximum number of named server instances that can be registered
	  in the static shell registry.

config NANOMODBUS_SHELL_MAX_DATA_WORDS
	int "Maximum data words per CLI transaction"
	depends on NANOMODBUS_SHELL_CLIENT
	default 64
	range 1 125
	help
	  Upper limit of 16-bit register words or coils that can be read or
	  written in a single shell command. This bounds the local stack buffer
	  allocated during command execution. The Modbus protocol limit for
	  Read Holding Registers (FC 03) is 125 registers (250 bytes).

endif # NANOMODBUS_SHELL
```

### 3.2 Kconfig Dependency Matrix

| Kconfig Symbol | Type | Default | Dependencies | Impact on Build |
| :--- | :--- | :--- | :--- | :--- |
| `CONFIG_NANOMODBUS_SHELL` | bool | `y` | `SHELL && NANOMODBUS` | Compiles `nanomodbus_z_shell.c`; registers root `nmbs` command. |
| `CONFIG_NANOMODBUS_SHELL_CLIENT` | bool | `y` | `NANOMODBUS_SHELL && NANOMODBUS_CLIENT` | Compiles client subcommands (`read-*`, `write-*`) and client registry logic. |
| `CONFIG_NANOMODBUS_SHELL_SERVER` | bool | `y` | `NANOMODBUS_SHELL && NANOMODBUS_SERVER` | Compiles server diagnostic subcommands (`status`, `reset-stats`) and server registry logic. |
| `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS` | int | `4` | `NANOMODBUS_SHELL_CLIENT` | Sizes static client descriptor array: `sizeof(client_desc) * MAX_CLIENTS`. |
| `CONFIG_NANOMODBUS_SHELL_MAX_SERVERS` | int | `4` | `NANOMODBUS_SHELL_SERVER` | Sizes static server descriptor array: `sizeof(server_desc) * MAX_SERVERS`. |
| `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS`| int | `64` | `NANOMODBUS_SHELL_CLIENT` | Allocates temporary stack buffers: `uint16_t data[MAX_DATA_WORDS]`. |

### 3.3 Build System Integration (`zephyr/CMakeLists.txt`)

The shell source file is conditionally compiled based on `CONFIG_NANOMODBUS_SHELL`:

```cmake
# zephyr/CMakeLists.txt update
zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_SHELL src/nanomodbus_z_shell.c)
```

---

## 4. Public Interface Contract (`nanomodbus_z_shell.h`)

The header file `zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h` defines the complete public API for the shell integration.

### 4.1 Header Contract Specification

```c
/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nanomodbus_z_shell.h
 * @brief Public interface and registry contracts for nanoMODBUS Zephyr Shell integration.
 * @ingroup nanomodbus_zephyr
 */

#ifndef ZEPHYR_INCLUDE_NANOMODBUS_Z_SHELL_H_
#define ZEPHYR_INCLUDE_NANOMODBUS_Z_SHELL_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>

#ifdef CONFIG_NANOMODBUS_SERVER_RTU
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length for a registered shell instance name, including NUL terminator. */
#define NMBS_Z_SHELL_NAME_MAX_LEN 16

/**
 * @brief Modbus server operational state.
 */
enum nmbs_z_server_state {
    NMBS_Z_SERVER_STATE_STOPPED = 0, /**< Server engine is inactive */
    NMBS_Z_SERVER_STATE_RUNNING = 1, /**< Server engine is actively listening/polling */
    NMBS_Z_SERVER_STATE_ERROR   = 2  /**< Server encountered an unrecoverable fault */
};

/**
 * @brief Server diagnostic statistics counters.
 *
 * All counters are monotonically increasing 32-bit unsigned integers.
 */
struct nmbs_z_server_stats {
    uint32_t rx_frames;         /**< Total Modbus request frames received */
    uint32_t tx_frames;         /**< Total Modbus response frames transmitted */
    uint32_t rx_errors;         /**< Checksum (CRC), framing, or framing silence errors */
    uint32_t exceptions_sent;   /**< Modbus exception responses sent to clients */
    uint32_t requests_handled;  /**< Valid Modbus requests successfully processed */
};

/**
 * @brief Server information and diagnostic snapshot.
 */
struct nmbs_z_server_info {
    enum nmbs_z_server_state state;  /**< Operational state of the server */
    uint8_t unit_id;                 /**< Configured slave/unit address (RTU) or 0 (TCP) */
    uint16_t port;                   /**< TCP port (if TCP server) or 0 (RTU) */
    struct nmbs_z_server_stats stats;/**< Diagnostic counters snapshot */
};

/**
 * @brief Diagnostic query callback for Modbus server instances.
 *
 * @param server_ctx Opaque pointer to the server instance context.
 * @param info Output pointer to receive server operational info and statistics.
 * @param reset If true, statistics counters are atomically reset to zero after reading.
 * @return 0 on success, negative errno on failure.
 */
typedef int (*nmbs_z_server_diag_cb)(void *server_ctx, struct nmbs_z_server_info *info, bool reset);

#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT

/**
 * @brief Register a nanoMODBUS client instance with the shell CLI.
 *
 * Makes the client available for interactive shell commands. If this is the
 * first or only client registered, it becomes the default target when the
 * `-c <client>` option is omitted in CLI commands.
 *
 * @param name Unique human-readable name for the client (e.g., "rtu0", "tcp_ext").
 *             Must not exceed NMBS_Z_SHELL_NAME_MAX_LEN - 1 characters.
 * @param client Pointer to initialized nmbs_z_client_t instance. Must remain valid
 *               for the entire duration of registration.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name or @p client is NULL, or @p name is empty/too long.
 * @retval -EEXIST if a client with @p name is already registered.
 * @retval -ENOMEM if the client registry is full (CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS).
 */
int nmbs_z_shell_register_client(const char *name, nmbs_z_client_t *client);

/**
 * @brief Unregister a nanoMODBUS client instance from the shell CLI.
 *
 * @param name Name of the registered client instance to remove.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name is NULL.
 * @retval -ENOENT if no client matching @p name was found in the registry.
 */
int nmbs_z_shell_unregister_client(const char *name);

#endif /* CONFIG_NANOMODBUS_SHELL_CLIENT */

#ifdef CONFIG_NANOMODBUS_SHELL_SERVER

/**
 * @brief Register a generic Modbus server instance with custom diagnostic callback.
 *
 * @param name Unique human-readable name for the server (e.g., "srv_rtu", "srv_tcp").
 *             Must not exceed NMBS_Z_SHELL_NAME_MAX_LEN - 1 characters.
 * @param server_ctx Opaque pointer to server instance.
 * @param diag_cb Callback invoked by `nmbs status` and `nmbs reset-stats`.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name, @p server_ctx, or @p diag_cb is NULL.
 * @retval -EEXIST if a server with @p name is already registered.
 * @retval -ENOMEM if the server registry is full (CONFIG_NANOMODBUS_SHELL_MAX_SERVERS).
 */
int nmbs_z_shell_register_server(const char *name, void *server_ctx, nmbs_z_server_diag_cb diag_cb);

/**
 * @brief Unregister a Modbus server instance from the shell CLI.
 *
 * @param name Name of the registered server instance to remove.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name is NULL.
 * @retval -ENOENT if no server matching @p name was found in the registry.
 */
int nmbs_z_shell_unregister_server(const char *name);

#ifdef CONFIG_NANOMODBUS_SERVER_RTU
/**
 * @brief Helper to register a standard Modbus RTU Server with built-in diagnostics.
 *
 * @param name Unique name for the RTU server instance (e.g., "rtu_srv").
 * @param server Pointer to initialized nmbs_z_server_rtu_t instance.
 *
 * @retval 0 on success, negative errno on failure.
 */
int nmbs_z_shell_register_server_rtu(const char *name, nmbs_z_server_rtu_t *server);
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
/**
 * @brief Helper to register a standard Modbus TCP Server with built-in diagnostics.
 *
 * @param name Unique name for the TCP server instance (e.g., "tcp_srv").
 * @param server Pointer to initialized nmbs_z_server_tcp_t instance.
 *
 * @retval 0 on success, negative errno on failure.
 */
int nmbs_z_shell_register_server_tcp(const char *name, nmbs_z_server_tcp_t *server);
#endif

#endif /* CONFIG_NANOMODBUS_SHELL_SERVER */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_SHELL_H_ */
```

---

## 5. Command Tree & Execution Semantics

### 5.1 Command Tree Hierarchy

All commands reside under the root command `nmbs`:

```
nmbs
├── list                                                     [Lists registered clients & servers]
├── read-coils       [-c <client>] [-u <unit>] <addr> <qty>  [FC 01: Read Coils]
├── read-discrete    [-c <client>] [-u <unit>] <addr> <qty>  [FC 02: Read Discrete Inputs]
├── read-holding     [-c <client>] [-u <unit>] <addr> <qty>  [FC 03: Read Holding Registers]
├── read-input       [-c <client>] [-u <unit>] <addr> <qty>  [FC 04: Read Input Registers]
├── write-coil       [-c <client>] [-u <unit>] <addr> <val>  [FC 05: Write Single Coil]
├── write-register   [-c <client>] [-u <unit>] <addr> <val>  [FC 06: Write Single Register]
├── write-coils      [-c <client>] [-u <unit>] <addr> <v...> [FC 15: Write Multiple Coils]
├── write-registers  [-c <client>] [-u <unit>] <addr> <v...> [FC 16: Write Multiple Registers]
├── status           [-s <server>]                           [Inspect server metrics & state]
└── reset-stats      [-s <server>]                           [Reset server diagnostic counters]
```

### 5.2 Command Details & Usage Specifications

#### 5.2.1 `nmbs list`
- **Purpose**: Displays all registered client and server instances, their types, configuration parameters, and default selection status.
- **Syntax**: `nmbs list`
- **Output Example**:
```
nanoMODBUS Registered Instances:
Clients:
  [0] 'rtu0' (default) -> Target Unit: 1, Timeout: 1000 ms, Byte Timeout: 100 ms
  [1] 'tcp0'           -> Target Unit: 1, Timeout: 5000 ms, Byte Timeout: 100 ms
Servers:
  [0] 'srv_rtu' (default) -> Type: RTU, Address: 1, State: RUNNING
  [1] 'srv_tcp'           -> Type: TCP, Port: 502, State: RUNNING
```

#### 5.2.2 `nmbs read-holding` (FC 03)
- **Purpose**: Read 1 to $N$ contiguous Holding Registers.
- **Syntax**: `nmbs read-holding [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <quantity>`
- **Parameters**:
  - `address`: Starting register address (0 - 65535, decimal or `0x` hex).
  - `quantity`: Number of registers (1 - `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS`).
  - `-c <client>`: Target client instance name. Optional if only 1 client is registered.
  - `-u <unit>`: Modbus slave unit address (1 - 247). Optional, defaults to client's configured unit ID or 1.
  - `-t <timeout_ms>`: Mutex lock & response timeout in ms. Optional, defaults to client configuration.
- **Output Format**: Displays 4 registers per line with decimal and hexadecimal formats:
```
[0x0010 / 00016]:   1000 (0x03E8)   2000 (0x07D0)      0 (0x0000)  65535 (0xFFFF)
[0x0014 / 00020]:     42 (0x002A)    128 (0x0080)
```

#### 5.2.3 `nmbs read-input` (FC 04)
- **Purpose**: Read 1 to $N$ contiguous Input Registers.
- **Syntax**: `nmbs read-input [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <quantity>`
- **Output Format**: Identical tabular format as `read-holding`.

#### 5.2.4 `nmbs read-coils` (FC 01)
- **Purpose**: Read 1 to $N$ contiguous discrete output coils.
- **Syntax**: `nmbs read-coils [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <quantity>`
- **Parameters**: `quantity` (1 - 2000, bounded by stack limit `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS * 16`).
- **Output Format**: Displays bit values in groups of 8:
```
[0x0000 / 00000]:  1  0  1  1  0  0  1  0  (0x4D)
[0x0008 / 00008]:  0  1  0  0              (0x02)
```

#### 5.2.5 `nmbs read-discrete` (FC 02)
- **Purpose**: Read 1 to $N$ contiguous Discrete Inputs.
- **Syntax**: `nmbs read-discrete [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <quantity>`
- **Output Format**: Identical bitfield format as `read-coils`.

#### 5.2.6 `nmbs write-coil` (FC 05)
- **Purpose**: Force a single coil to ON or OFF.
- **Syntax**: `nmbs write-coil [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <value>`
- **Accepted Values**: `1`, `0`, `on`, `off`, `true`, `false`, `HIGH`, `LOW` (case-insensitive).
- **Output Example**:
```
OK: Wrote coil at address 0x0020 (32) = ON on client 'rtu0' (unit 1)
```

#### 5.2.7 `nmbs write-register` (FC 06)
- **Purpose**: Preset a single Holding Register value.
- **Syntax**: `nmbs write-register [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <value>`
- **Parameters**: `value` (0 - 65535, decimal or `0x` hex).
- **Output Example**:
```
OK: Wrote register at address 0x0010 (16) = 1234 (0x04D2) on client 'rtu0' (unit 1)
```

#### 5.2.8 `nmbs write-coils` (FC 15)
- **Purpose**: Force multiple contiguous coils in a single request.
- **Syntax**: `nmbs write-coils [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <val0> [val1 ... valN]`
- **Output Example**:
```
OK: Wrote 4 coil(s) starting at address 0x0000 (0) on client 'rtu0' (unit 1)
```

#### 5.2.9 `nmbs write-registers` (FC 16)
- **Purpose**: Preset multiple contiguous Holding Registers in a single request.
- **Syntax**: `nmbs write-registers [-c <client>] [-u <unit>] [-t <timeout_ms>] <address> <val0> [val1 ... valN]`
- **Output Example**:
```
OK: Wrote 3 register(s) starting at address 0x0100 (256) on client 'rtu0' (unit 1)
```

#### 5.2.10 `nmbs status`
- **Purpose**: Display operational metrics and error counters for a registered server.
- **Syntax**: `nmbs status [-s <server>]`
- **Output Example**:
```
Modbus Server 'srv_rtu' Status:
  Operational State:  RUNNING
  Modbus Unit ID:     1
  Frames Received:    14820
  Frames Transmitted: 14815
  Checksum Errors:    5
  Exceptions Sent:    12
  Requests Handled:   14803
```

#### 5.2.11 `nmbs reset-stats`
- **Purpose**: Atomically reset server diagnostic error and frame counters to zero.
- **Syntax**: `nmbs reset-stats [-s <server>]`
- **Output Example**:
```
OK: Diagnostic counters for server 'srv_rtu' reset to 0.
```

---

### 5.3 Option Flag Parsing & Target Instance Selection

To support robust parsing without requiring the optional `CONFIG_SHELL_GETOPT=y` symbol, `nanomodbus_z_shell.c` implements a deterministic scan of `argv`:

```c
struct cli_options {
    const char *client_name;
    const char *server_name;
    uint8_t unit_id;
    bool unit_id_specified;
    int32_t timeout_ms;
    size_t positional_idx[4];
    size_t positional_count;
};
```

1. **Option Recognition**: Arguments beginning with `-` are inspected:
   - `-c`: Sets `client_name = argv[++i]`.
   - `-s`: Sets `server_name = argv[++i]`.
   - `-u`: Parses `unit_id` via `strtoul(argv[++i], NULL, 0)`.
   - `-t`: Parses `timeout_ms` via `strtol(argv[++i], NULL, 0)`.
   - Unrecognized option flags immediately produce `shell_error(sh, "Unknown option: %s", argv[i])` and return `-EINVAL`.
2. **Positional Arguments**: Non-flag arguments are gathered in order. If mandatory argument count is unsatisfied, `shell_help(sh)` is invoked.
3. **Instance Resolution Rules**:
   - If `-c <client>` is provided: The registry searches for a matching name. If not found, `shell_error(sh, "Client '%s' not found. Run 'nmbs list'", name)` is returned.
   - If `-c` is omitted:
     - When exactly 1 client is registered: Automatically binds to that instance.
     - When 0 clients are registered: `shell_error(sh, "No Modbus client registered.")` (-ENODEV).
     - When $>1$ clients are registered: `shell_error(sh, "Multiple clients registered. Specify target with -c <client_name>.")` (-EINVAL).

---

### 5.4 Radix Handling & Input Validation

1. **Radix Conversion**: Numerical parameters (addresses, quantities, values) are parsed using `strtoul(str, &endptr, 0)`. Base `0` automatically handles:
   - Decimal: `100` -> `100`
   - Hexadecimal: `0x64` or `0X64` -> `100`
   - Octal: `0144` -> `100`
   If `*endptr != '\0'` or the string is empty, parsing fails with `shell_error(sh, "Invalid number format: '%s'", str)`.
2. **Boundary Validation**:
   - Register address: `0 <= addr <= 0xFFFF` (65535).
   - Register quantity: `1 <= qty <= CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS`.
   - Coil quantity: `1 <= qty <= (CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS * 16)`.
   - Unit ID: `1 <= unit <= 247` (or `0` for broadcast on write commands).
   - Register value: `0 <= val <= 0xFFFF`.

---

### 5.5 Console Output Formatting & Modbus Error Mapping

When a Modbus request fails, the shell MUST provide full diagnostic clarity:

```c
int ret = nmbs_z_client_read_holding_registers(client, addr, qty, buf, timeout);
if (ret < 0) {
    nmbs_error raw_err = nmbs_z_client_get_last_error(client);
    if (nmbs_error_is_exception(raw_err)) {
        shell_error(sh, "Modbus Exception: %s (Code 0x%02X)",
                    nmbs_z_strerror(raw_err), (uint8_t)raw_err);
    } else {
        shell_error(sh, "Communication Error: %s (errno %d)",
                    nmbs_z_strerror(raw_err), ret);
    }
    return ret;
}
```

This ensures the user immediately knows whether the failure was a physical bus timeout, a CRC error, or a logical Modbus exception (e.g. `ILLEGAL_DATA_ADDRESS`).

---

## 6. Concurrency, Synchronization & Memory Footprint

### 6.1 Execution Thread Context

```
+-------------------------------------------------------------+
|                Zephyr Shell Backend Thread                  |
|                Priority: CONFIG_SHELL_STACK_SIZE            |
+------------------------------+------------------------------+
                               | Executes CLI Handler
                               v
+-------------------------------------------------------------+
|     nmbs_z_shell handler (e.g. cmd_read_holding)            |
+------------------------------+------------------------------+
                               | Calls nmbs_z_client helper
                               v
+-------------------------------------------------------------+
|                nmbs_z_client_t Instance                     |
|                                                             |
|   k_mutex_lock(&client->lock, timeout);                     |
|   ========================================================= |
|   CRITICAL SECTION:                                         |
|   - Sends request PDU via platform transport callback       |
|   - Waits for response bytes or timeout                     |
|   - Verifies CRC-16 and frame consistency                   |
|   ========================================================= |
|   k_mutex_unlock(&client->lock);                            |
+-------------------------------------------------------------+
```

1. **Thread Context**: Handlers execute synchronously in the context of the calling shell thread (e.g. `shell_uart`).
2. **Serialization via Mutex**:
   - `nmbs_z_client_t` contains an embedded `struct k_mutex lock`.
   - All client read/write helpers begin with `k_mutex_lock(&client->lock, lock_timeout)` and exit with `k_mutex_unlock(&client->lock)`.
   - If an application background thread (e.g. a sensor polling task) is actively executing a Modbus transaction, the shell thread blocks cleanly until the transaction finishes or the lock timeout expires.
   - If the lock timeout expires (default 1000 ms), the shell outputs `shell_error(sh, "Client busy: Lock timeout expired")` and aborts without corrupting ongoing traffic.
3. **Deadlock Free Guarantee**: The shell handler acquires only ONE lock (`client->lock`) and performs no recursive locking or nested synchronization.

### 6.2 Server Metrics Thread Safety & Atomics

Modbus servers process incoming frames asynchronously (via dedicated workqueues in RTU or listener threads in TCP). To avoid introducing locks into time-sensitive ISRs or work items:
- All server diagnostic counters are updated using Zephyr atomic operations: `atomic_inc()`, `atomic_add()`, and `atomic_set()`.
- The shell `nmbs status` command retrieves counter values using `atomic_get()`.
- The `nmbs reset-stats` command atomically exchanges counter values with `0` using `atomic_set()`.
- Zero lock contention or blocking is imposed on real-time Modbus server processing.

### 6.3 Memory Footprint & Zero Dynamic Allocation Guarantee

1. **Static Registry Allocation**:
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

- With `MAX_CLIENTS = 4` and `MAX_SERVERS = 4`:
  - `s_clients`: $4 \times (16 + 4) = 80$ bytes (32-bit architecture)
  - `s_servers`: $4 \times (16 + 4 + 4) = 96$ bytes
  - Total static RAM overhead: **$< 200$ bytes**.

2. **Stack Budget & Bounds**:
   - Register data buffer: `uint16_t data[CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS]`. At default 64 words, this requires exactly $128$ bytes of stack space.
   - Bitfield buffer for coils: `nmbs_bitfield coils;` requires $32$ bytes.
   - Total stack allocation per shell handler invocation: $< 280$ bytes.
   - Fits safely within standard Zephyr shell thread stacks (`CONFIG_SHELL_STACK_SIZE=2048`).

3. **Code (ROM) Size**:
   - The compiled `nanomodbus_z_shell.o` object requires approximately $2.6$ to $3.2$ KB of flash memory.

---

## 7. Test & Verification Plan

### 7.1 Automated Verification with Dummy Shell Backend

Verification is executed under the Zephyr Ztest framework in `tests/zephyr/src/test_shell.c`. The test suite utilizes Zephyr's built-in **Dummy Shell Backend** (`CONFIG_SHELL_BACKEND_DUMMY=y`).

The dummy backend allows programmatic injection of shell command strings and extraction of console output buffers without requiring physical UART hardware:

```c
#include <zephyr/ztest.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>

static const struct shell *sh;

static void setup(void *fixture)
{
    sh = shell_backend_dummy_get_ptr();
    zassert_not_null(sh, "Dummy shell not initialized");
}
```

### 7.2 Detailed Test Matrix

| Test Suite / Category | Test Case Identifier | Objective & Verification Condition |
| :--- | :--- | :--- |
| **Registry Lifecycle** | `test_shell_register_client_success` | Register client with valid name; verify `0` returned and instance listed. |
| | `test_shell_register_client_duplicate` | Register two clients with identical name; verify second returns `-EEXIST`. |
| | `test_shell_register_client_boundary` | Register up to `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS`; verify `MAX + 1` returns `-ENOMEM`. |
| | `test_shell_unregister_client` | Unregister existing client; verify `-ENOENT` when attempting to unregister again. |
| | `test_shell_default_resolution` | Verify single registered client is auto-selected; verify error when $>1$ registered and `-c` omitted. |
| | `test_shell_register_server` | Register server instance; verify `nmbs list` includes server name and state. |
| **Argument Validation** | `test_shell_missing_args` | Execute `nmbs read-holding`; verify syntax help output and `-EINVAL`. |
| | `test_shell_invalid_radix` | Execute `nmbs read-holding 0xZZ 4`; verify parse error message. |
| | `test_shell_out_of_bounds_qty`| Execute `nmbs read-holding 0 200`; verify quantity limit error. |
| | `test_shell_invalid_coil_val` | Execute `nmbs write-coil 0 invalid`; verify boolean error message. |
| **Functional Execution** | `test_shell_cmd_read_holding_fc03` | Mock transport returns 2 registers; verify shell dummy output contains formatted values. |
| | `test_shell_cmd_read_coils_fc01` | Mock transport returns 8 coils; verify binary bitfield output. |
| | `test_shell_cmd_write_reg_fc06` | Execute `nmbs write-register 0x10 0x04D2`; verify mock receives FC 06 PDU. |
| | `test_shell_cmd_write_regs_fc16`| Execute `nmbs write-registers 0x10 10 20 30`; verify mock receives FC 16 PDU with 3 words. |
| **Error & Exception** | `test_shell_modbus_exception` | Mock responds with Exception 0x02; verify shell output reports `Modbus Exception: Illegal data address (02)`. |
| | `test_shell_timeout_reporting` | Mock simulates transport timeout; verify shell reports communication timeout error. |
| | `test_shell_lock_contention` | Background thread locks `client->lock`; verify shell reports lock timeout without hanging. |

### 7.3 Reference Test Implementation Snippet

```c
ZTEST(nanomodbus_shell, test_shell_read_holding_success)
{
    size_t out_len;
    const char *output;

    /* Prepare mock server register values */
    mock_registers[0] = 0x1234;
    mock_registers[1] = 0x5678;

    /* Clear previous console buffer */
    shell_backend_dummy_clear_output(sh);

    /* Execute shell command */
    int ret = shell_execute_cmd(sh, "nmbs read-holding 0 2");
    zassert_equal(ret, 0, "Command execution failed: %d", ret);

    /* Validate output content */
    output = shell_backend_dummy_get_output(sh, &out_len);
    zassert_true(strstr(output, "0x1234") != NULL, "Output missing reg 0 value");
    zassert_true(strstr(output, "0x5678") != NULL, "Output missing reg 1 value");
}
```

---

## 8. Spec Self-Review Checklist

| Architectural Invariant / Requirement | Compliance Verification | Status |
| :--- | :--- | :--- |
| **Upstream Purity** | Core files `nanomodbus.c` and `nanomodbus.h` remain completely untouched. Zero RTOS or shell headers in core. | **PASSED** |
| **Zero `printk` / Zero `printf`** | All CLI output uses `shell_print()`, `shell_error()`, `shell_warn()`. Zero `printk` / `printf` in code. | **PASSED** |
| **Zero Dynamic Allocation** | Static arrays for registries (`MAX_CLIENTS`, `MAX_SERVERS`). Stack buffers bounded by `MAX_DATA_WORDS`. Zero `k_malloc`. | **PASSED** |
| **Thread-Safety & Synchronization** | Client operations serialize via `client->lock` (`k_mutex`). Server metrics use lock-free `atomic_t`. | **PASSED** |
| **Multi-Instance Support** | Named registries with `-c <client>` and `-s <server>` flags. Automatic single-instance default fallback. | **PASSED** |
| **Radix Flexibility** | Decimal and Hexadecimal (`0x...`) supported for all address, quantity, and value arguments. | **PASSED** |
| **Modbus Error Translation** | Unmodified `nmbs_error` retrieved via `nmbs_z_client_get_last_error()` and mapped via `nmbs_z_strerror()`. | **PASSED** |
| **Automated Testability** | Comprehensive Ztest suite using `CONFIG_SHELL_BACKEND_DUMMY=y` requiring zero physical hardware. | **PASSED** |
| **Completeness & Ambiguity** | Zero TBDs, zero TODOs, exact prototypes, concrete command table, complete data types. | **PASSED** |
