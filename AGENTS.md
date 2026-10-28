# AGENTS.md

nanoMODBUS (compact C99 Modbus RTU/TCP library) plus a native Zephyr external
module. Work happens on `feature/zephyr-module`.

## Architecture & Component Boundaries

- `nanomodbus.c` / `nanomodbus.h` are the upstream protocol core. Keep them pure
  ISO C99: no `<zephyr/...>` includes, no `k_*` calls, no `CONFIG_*`. All RTOS
  and hardware code belongs in `zephyr/`.
- `zephyr/src/` implements Zephyr integration with no heap allocation (state lives in caller structs):
  - `nanomodbus_z_client.c`: Mutex-wrapped client (`k_mutex`, runs on caller thread).
  - `nanomodbus_z_server_rtu.c`: Event-driven RTU server (`k_timer` + `k_work`).
  - `nanomodbus_z_server_tcp.c`: Active Object TCP server (dedicated `k_thread`).
  - `nanomodbus_z_transport_serial.c`: UART transport (RS-485 DE pin control, t1.5/t3.5 frame timings).
  - `nanomodbus_z_transport_tcp.c`: TCP transport (Zephyr BSD sockets API).
  - `nanomodbus_z_shell.c`: Zephyr Shell CLI commands, static instance registry, radix parser, and table formatting.
  - `nanomodbus_z_common.c`: POSIX error code mapping (`nmbs_error_to_errno`, `errno_to_nmbs_error`).
- Public headers: `zephyr/include/zephyr/nanomodbus/nanomodbus_z{,_serial,_tcp,_shell}.h`,
  included as `<zephyr/nanomodbus/nanomodbus_z.h>`, `<zephyr/nanomodbus/nanomodbus_z_shell.h>`, etc.
- `zephyr/Kconfig` defines the feature tree; root `Kconfig` only `rsource`s it.
  `zephyr/module.yml` registers the module.
- Zephyr apps are in `samples/` (`server_rtu`, `client_rtu`, `client_tcp`, `server_tcp`, `modbus_gateway`).
  `examples/` contains upstream non-Zephyr code (Linux, Arduino, RP2040, STM32, Win32).
- Design spec: `docs/superpowers/specs/2026-10-23-zephyr-module-integration-design.md`.
  Its snippets and `examples/server_rtu` paths are partly stale; trust the tree.

## Hardware & Devicetree Overlays

Target board is `nucleo_h743zi` (STM32H743ZI), not `nucleo_h743`. Each sample
provides board configurations in `samples/<app>/boards/nucleo_h743zi.overlay` and `.conf`:

- `server_rtu`: `&usart2` (PD5/PD6, 115200) aliased to `modbus-uart`; RS-485 DE pin on `&gpiod 4` aliased to `modbus-de` (or hardware RTS DE via pinctrl). Requires `CONFIG_UART_INTERRUPT_DRIVEN=y`.
- `client_rtu`: `&usart2` (PD5/PD6, 115200) aliased to `modbus-uart`.
- `client_tcp` / `server_tcp`: Ethernet MAC `&mac` with DHCP (`CONFIG_NET_L2_ETHERNET=y`, `CONFIG_NET_DHCPV4=y`).
- `modbus_gateway`: Dual UARTs `&usart2` (`modbus-bus1`) and `&uart4` (`modbus-bus2`, PC10/PC11), plus Ethernet `&mac`.

## Logging Rules

- **Strict Zephyr logging:** All Zephyr code and samples MUST use `<zephyr/logging/log.h>` (`LOG_INF`, `LOG_DBG`, `LOG_ERR`, `LOG_WRN`).
- **ZERO `printf` and ZERO `printk`:** Never use `printf` or `printk` in `zephyr/` or `samples/`.
- Log level is controlled via Kconfig: `CONFIG_NANOMODBUS_LOG_LEVEL`. Register modules via `LOG_MODULE_REGISTER(name, CONFIG_NANOMODBUS_LOG_LEVEL)`.

## Environment

```sh
source /home/jeffry/.zephyr-venv/bin/activate
source /home/jeffry/zephyrproject/zephyr/zephyr-env.sh
export ZEPHYR_SDK_INSTALL_DIR=/home/jeffry/zephyr-sdk-1.0.1
```

- Target board is `nucleo_h743zi` (STM32H743ZI).
- This repo is outside the west workspace, so register it explicitly on every
  Zephyr build. Without this, Kconfig warns `undefined symbol NANOMODBUS` and
  the build fails:
  - west build: `-- -DEXTRA_ZEPHYR_MODULES=$PWD`
  - twister: `--extra-args ZEPHYR_MODULES=$PWD`
- Use explicit build dirs (`-d build_<app>`); `build/` is an existing Zephyr
  build for `samples/server_rtu`, and west rejects stale dirs without `-p always`.

## Host Build and Tests

```sh
cmake -S . -B build_host -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host -j
ctest --test-dir build_host --output-on-failure
```

- `BUILD_TESTS` and `BUILD_EXAMPLES` default to OFF; bare `cmake ..` only builds the library.
- `ctest` takes ~70 s; `test_nanomodbus` (~40 s) and `test_multi_server_rtu`
  (~26 s) use real sleeps. This is not a hang.
- Run one test: `ctest --test-dir build_host -R test_nanomodbus --output-on-failure`.

## Zephyr Build and Tests

```sh
# Build a sample
west build -p always -b nucleo_h743zi samples/client_tcp -d build_client_tcp \
  -- -DEXTRA_ZEPHYR_MODULES=$PWD

# Full Ztest suite (native_sim only)
west twister -T tests/zephyr -p native_sim --extra-args ZEPHYR_MODULES=$PWD
```

- `tests/zephyr/testcase.yaml` allows `native_sim` only; Twister filters other
  boards. All 18 tests currently pass.
- All `tests/zephyr/src/*.c` files link into one test binary under the single
  `nanomodbus.baseline` Twister scenario. Add new test files to `tests/zephyr/CMakeLists.txt`.
- Run single suites directly on `native_sim` without Twister:
  ```sh
  west build -p always -b native_sim tests/zephyr -d build_ztest \
    -- -DEXTRA_ZEPHYR_MODULES=$PWD
  ./build_ztest/zephyr/zephyr.exe -list
  ./build_ztest/zephyr/zephyr.exe -test=<suite>::*
  ```
  Registered suites:
  - `nanomodbus_baseline`
  - `nanomodbus_client`
  - `nanomodbus_error`
  - `nanomodbus_multi_instance`
  - `nanomodbus_serial`
  - `nanomodbus_server_rtu`
  - `nanomodbus_server_tcp`
  - `nanomodbus_shell`
  Suite names differ from source file names; verify with `-list`.

## Shell CLI Integration

nanoMODBUS provides an interactive Zephyr Shell CLI (`nmbs`) supporting client
request execution, server diagnostics, and instance enumeration without dynamic memory allocation:

- **Kconfig:** `CONFIG_NANOMODBUS_SHELL=y` enables the subsystem (`CONFIG_NANOMODBUS_SHELL_CLIENT`, `CONFIG_NANOMODBUS_SHELL_SERVER`, `MAX_CLIENTS`, `MAX_SERVERS`, `MAX_DATA_WORDS`).
- **Registration APIs (`<zephyr/nanomodbus/nanomodbus_z_shell.h>`):**
  - `nmbs_z_shell_register_client("rtu0", &client)`
  - `nmbs_z_shell_register_server_rtu("srv0", &server)`
  - `nmbs_z_shell_register_server_tcp("tcp0", &server)`
  - `nmbs_z_shell_register_server("name", ctx, diag_cb)`
- **Commands:**
  - `nmbs list`: enumerate all registered client and server instances.
  - `nmbs read-holding [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>` (FC 03)
  - `nmbs read-input [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>` (FC 04)
  - `nmbs read-coils [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>` (FC 01)
  - `nmbs read-discrete [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>` (FC 02)
  - `nmbs write-coil [-c <client>] [-u <unit>] [-t <ms>] <addr> <on|off|1|0>` (FC 05)
  - `nmbs write-register [-c <client>] [-u <unit>] [-t <ms>] <addr> <val>` (FC 06)
  - `nmbs write-coils [-c <client>] [-u <unit>] [-t <ms>] <addr> <val0> [val1...]` (FC 15)
  - `nmbs write-registers [-c <client>] [-u <unit>] [-t <ms>] <addr> <val0> [val1...]` (FC 16)
  - `nmbs status [-s <server>]`: display server operational metrics and error counters.
  - `nmbs reset-stats [-s <server>]`: reset server diagnostic statistics counters to 0.

## Gotchas

- RS-485 DE is deasserted after waiting for UART TX completion only when
  `CONFIG_UART_INTERRUPT_DRIVEN=y`; without it the code waits just one character
  time, so custom board configs must enable it or DE may drop while bytes are
  still queued (`zephyr/src/nanomodbus_z_transport_serial.c`).
- `nmbs_z_server_tcp_t` accepts **one client at a time** (listen backlog 1,
  single request loop). "Threaded" means it runs on its own thread, not that it
  serves concurrent clients. All instances also share one file-scope stack
  (`default_tcp_server_stack`); do not start two TCP servers concurrently.
- Adding a Kconfig prune option requires changes in three places:
  `zephyr/Kconfig`, the `zephyr_compile_definitions_ifdef` block in
  `zephyr/CMakeLists.txt`, and the `CONFIG_* -> NMBS_*` block in
  `zephyr/include/zephyr/nanomodbus/nanomodbus_z.h` (the header is what apps see).
- Per-function-code `NMBS_CLIENT_*_DISABLED` macros are defined but not honored
  by the upstream core; only `NMBS_CLIENT_DISABLED` and the `NMBS_SERVER_*`
  macros actually prune code.
- GitHub CI (`ci.yml`, `workflow_dispatch` only) builds/tests the host CMake
  project and builds the Arduino/RP2040/STM32 examples, but never the Zephyr
  module. Validate Zephyr builds/tests locally.
  `clang-format-check.yml` pins clang-format 13 (local is newer), so format
  touched files only.
- `AGENTS.md` is listed in `.gitignore`; changes here will not appear in
  `git status` unless force-added (`git status --ignored` or `git add -f AGENTS.md`).
