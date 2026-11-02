# nanoMODBUS Shell CLI

nanoMODBUS ships an interactive [Zephyr Shell](https://docs.zephyrproject.org/latest/services/shell/index.html)
command tree named `nmbs`. It lets you enumerate registered Modbus instances, issue
client requests by function code, and read or reset server diagnostics from the
console, without writing application code and without dynamic allocation.

Everything is backed by a static registry: instance names and their pointers are
stored in fixed arrays sized at build time by `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS`
and `CONFIG_NANOMODBUS_SHELL_MAX_SERVERS`. No heap is used, and command output goes
through `shell_print()`/`shell_error()`.

## 1. Enabling the subsystem

```conf
# prj.conf
CONFIG_NANOMODBUS=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKENDS=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_NANOMODBUS_SHELL=y          # default y, but needs SHELL and NANOMODBUS
```

The command groups are pruned independently:

| Option | Default | Provides |
| --- | --- | --- |
| `CONFIG_NANOMODBUS_SHELL` | y | The `nmbs` subtree; depends on `SHELL && NANOMODBUS` |
| `CONFIG_NANOMODBUS_SHELL_CLIENT` | y | FC 01-06, 15, 16 commands; depends on `NANOMODBUS_CLIENT` |
| `CONFIG_NANOMODBUS_SHELL_SERVER` | y | `status`, `reset-stats`; depends on `NANOMODBUS_SERVER` |
| `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS` | 4 | Static client registry size, 1-8 |
| `CONFIG_NANOMODBUS_SHELL_MAX_SERVERS` | 4 | Static server registry size, 1-8 |
| `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS` | 64 | Per-command data bound, 1-125 |

`CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS` also sizes the stack buffer used by the
client commands, and caps the quantity argument of the read commands. The Modbus
protocol limit for FC 03 is 125 registers, so raising it beyond that buys nothing.

## 2. Registering instances

Include the public header and register each instance after it has been
initialized:

```c
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>

/* Client */
nmbs_z_shell_register_client("rtu0", &client);

/* Server, with built-in diagnostics */
#ifdef CONFIG_NANOMODBUS_SERVER_RTU
nmbs_z_shell_register_server_rtu("srv0", &server);
#endif
#ifdef CONFIG_NANOMODBUS_SERVER_TCP
nmbs_z_shell_register_server_tcp("tcp0", &server);
#endif

/* Server with a custom diagnostic callback */
nmbs_z_shell_register_server("custom", ctx, my_diag_cb);
```

Pairs are available to remove entries again: `nmbs_z_shell_unregister_client()` and
`nmbs_z_shell_unregister_server()`.

Names are unique strings of at most `NMBS_Z_SHELL_NAME_MAX_LEN - 1` (15)
characters. The pointed-to instance must stay valid for as long as it is
registered, which in practice means `static` storage in `main()` or at file scope.

Registration returns:

| Code | Meaning |
| --- | --- |
| `0` | Registered |
| `-EINVAL` | NULL/empty name or pointer, name too long |
| `-EEXIST` | A client or server with that name is already registered |
| `-ENOMEM` | Registry full (`MAX_CLIENTS` / `MAX_SERVERS`) |

When exactly one client (or one server) is registered it becomes the default
target, and the `-c` / `-s` option can be omitted.

## 3. Commands

```text
nmbs <command> [options] [arguments]
```

### 3.1 `nmbs list`

Prints the registry with each instance's target and live status.

```text
nmbs list
nanoMODBUS Registered Instances:
Clients:
  [0] 'rtu0' (default) -> Target Unit: 1, Timeout: 1000 ms, Byte Timeout: 100 ms
Servers:
  [0] 'srv0' (default) -> Type: RTU, Address: 1, State: RUNNING
```

TCP servers print `Type: TCP, Port: <n>` instead of the RTU address. `(default)`
appears only when that registry holds a single entry. Disabled command groups show
`(disabled)`.

### 3.2 Client commands

| Command | Function code | Arguments |
| --- | --- | --- |
| `nmbs read-coils` | 01 | `<addr> <qty>` |
| `nmbs read-discrete` | 02 | `<addr> <qty>` |
| `nmbs read-holding` | 03 | `<addr> <qty>` |
| `nmbs read-input` | 04 | `<addr> <qty>` |
| `nmbs write-coil` | 05 | `<addr> <on\|off\|1\|0>` |
| `nmbs write-register` | 06 | `<addr> <val>` |
| `nmbs write-coils` | 15 | `<addr> <val0> [val1...]` |
| `nmbs write-registers` | 16 | `<addr> <val0> [val1...]` |

Every client command accepts `-c <client>`, `-u <unit>` and `-t <ms>` before the
positional arguments:

```text
nmbs read-holding -c rtu0 -u 1 -t 250 0 4
[0x0000 / 00000]:     10 (0x000A)     20 (0x0014)      0 (0x0000)      0 (0x0000)
```

```text
nmbs write-coil 5 on
OK: Wrote coil at address 0x0005 (5) = ON on client 'rtu0' (unit 1)

nmbs write-registers 10 7 8 9
OK: Wrote 3 register(s) starting at address 0x000A (10) on client 'rtu0' (unit 1)
```

Read commands print four registers per line, or eight bits per line for the
bit-oriented function codes:

```text
nmbs read-coils 0 8
[0x0000 / 00000]:  1  0  1  0  0  0  0  0  (0x05)
```

### 3.3 Server diagnostics

| Command | Purpose |
| --- | --- |
| `nmbs status [-s <server>]` | Operational state and counters |
| `nmbs reset-stats [-s <server>]` | Set the counters back to zero |

```text
nmbs status -s srv0
Modbus Server 'srv0' Status:
  Operational State:  RUNNING
  Modbus Unit ID:     1
  Frames Received:    42
  Frames Transmitted: 42
  Checksum Errors:    0
  Exceptions Sent:    0
  Requests Handled:   42
```

TCP servers report `TCP Port:` in place of `Modbus Unit ID:`. State is one of
`STOPPED`, `RUNNING` or `ERROR`.

These two commands call the server's diagnostic callback with a reset flag, so a
custom server registered through `nmbs_z_shell_register_server()` must fill
`nmbs_z_server_info` and honour that flag.

## 4. Options

| Option | Commands | Behaviour |
| --- | --- | --- |
| `-c <client>` | FC 01-06, 15, 16 | Selects the client instance. Required when more than one is registered. |
| `-u <unit>` | FC 01-06, 15, 16 | Modbus unit ID, 0-247. Overrides the client's configured target address. When omitted the client's RTU destination address is used, falling back to 1 if that is 0. |
| `-t <ms>` | FC 01-06, 15, 16 | Timeout for both the client lock and the transaction. The client's previous read timeout is restored afterwards. Defaults to the client's read timeout, or 1000 ms. |
| `-s <server>` | `status`, `reset-stats` | Selects the server instance. Required when more than one is registered. |

Options may be interleaved with the positional arguments; the parser does not
require a fixed order. An unknown option or a missing option argument is rejected
with an explicit message.

## 5. Number formats and limits

- Addresses, unit IDs, values and timeouts are parsed with `strtoul(..., 0)`, so
  decimal and `0x`-prefixed hexadecimal both work. Negative numbers are rejected.
- Coil values accept `on`/`off`, `true`/`false`, `1`/`0` and `high`/`low`,
  case-insensitively.
- Read quantity must be between 1 and `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS`.
- `write-coils` accepts up to `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS * 16` values,
  capped at 2000.
- `write-registers` accepts up to `CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS` values.

## 6. Diagnostics and error output

Client commands distinguish three failure classes:

| Output | Cause |
| --- | --- |
| `Client busy: Lock timeout expired` | Another thread holds the client mutex |
| `Modbus Exception: <name> (Code 0xNN)` | The server answered with an exception |
| `Communication Error: <name> (errno N)` | Timeout, CRC/framing error, transport failure |

Resolution failures are reported before any traffic is attempted:

| Message | Fix |
| --- | --- |
| `No Modbus client registered.` | Call `nmbs_z_shell_register_client()` at startup |
| `Multiple clients registered. Specify target with -c <client_name>.` | Add `-c` |
| `Client 'x' not found. Run 'nmbs list'` | Check the name |
| `No Modbus server registered.` | Call `nmbs_z_shell_register_server*()` |
| `Multiple servers registered. Specify target with -s <server_name>.` | Add `-s` |

## 7. Using it with the samples

`samples/server_rtu` registers `srv0`; `samples/client_rtu` registers `rtu0`.
Samples that expose the shell leave it on the console UART, so on
`nucleo_h743zi` the shell is on the ST-LINK VCP (`usart3`, `/dev/ttyACM0`) while
the Modbus bus is `usart2`.

`samples/client_rtu` does **not** poll on its own: its FC03/FC06 loop is behind
`CONFIG_CLIENT_RTU_POLLING`, which defaults to `n`. The bus is therefore free for
`nmbs` commands or for an external master, and the client is already pointed at
slave 1, so `nmbs read-holding 0 4` works without extra options. Build with
`-DCONFIG_CLIENT_RTU_POLLING=y` to restore the automatic loop.

## 8. Implementation notes

The subcommand table in `zephyr/src/nanomodbus_z_shell.c` is wrapped in
`clang-format off`/`on`: clang-format parses hyphenated names such as
`read-coils` as a subtraction and rewrites them to `read - coils`, which silently
breaks every command. Do not remove that guard or reformat the block by hand.

Adding an instance beyond `CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS` or
`MAX_SERVERS` fails with `-ENOMEM`; the registries are fixed size by design.

## 9. See also

- [`docs/zephyr-getting-started.md`](zephyr-getting-started.md) for devicetree,
  RS-485 wiring, receive modes and the full Kconfig reference.
- [`zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h`](../zephyr/include/zephyr/nanomodbus/nanomodbus_z_shell.h)
  for the registration API and the `nmbs_z_server_info` structure.
- `samples/client_rtu` and `samples/server_rtu` for a working registration.
