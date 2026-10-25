# nanoMODBUS as a Zephyr module

This guide shows how to add nanoMODBUS to a Zephyr application as an external
module, enable only the roles and transports you need, and use the RTU and TCP
client/server APIs.

The portable protocol core (`nanomodbus.c` / `nanomodbus.h`) has no Zephyr
dependency. Everything RTOS- and hardware-specific lives in `zephyr/`:

| Path | Contents |
| --- | --- |
| `zephyr/module.yml` | Zephyr module registration (name `nanomodbus`) |
| `zephyr/Kconfig` | Feature tree (`CONFIG_NANOMODBUS*`) |
| `zephyr/CMakeLists.txt` | Builds the core and the enabled adapters |
| `zephyr/include/zephyr/nanomodbus/` | Public headers |
| `zephyr/src/` | Client wrapper, RTU/TCP transports, RTU/TCP server engines |
| `samples/` | Five runnable Zephyr applications |
| `tests/zephyr/` | Ztest suite (native_sim) |

## 1. Requirements

- Zephyr 4.4 or newer (the module is developed and tested against Zephyr 4.4).
- `west` and the Zephyr SDK.
- A UART for RTU, or a network interface and IP stack for TCP.
- No heap: all module state lives in caller-provided structures.

## 2. Add the module to the build

### 2.1 As a west project (recommended)

Add the module to the application manifest and let `west update` fetch it:

```yaml
# west.yml
manifest:
  remotes:
    - name: dowhile98
      url-base: https://github.com/dowhile98
  projects:
    - name: nanomodbus
      remote: dowhile98
      revision: feature/zephyr-module
      path: modules/lib/nanomodbus
  self:
    path: app
```

```sh
west update
```

The Zephyr module is currently developed on the `feature/zephyr-module` branch
of this repository. Once it is merged upstream, point the project at
`debevv/nanoMODBUS` and at the release tag you want to track. What matters is
that the selected revision contains `zephyr/module.yml`.

### 2.2 From an existing application (no manifest change)

Point Zephyr at the module before `find_package(Zephyr)`:

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES /absolute/path/to/nanoMODBUS)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_modbus_app)

target_sources(app PRIVATE src/main.c)
```

The same value can be passed on the command line:

```sh
west build -p always -b <board> . -d build \
  -- -DEXTRA_ZEPHYR_MODULES=/absolute/path/to/nanoMODBUS
```

A module that is already inside the west workspace (for example under
`modules/lib/nanomodbus` from a manifest) is discovered automatically. The
`EXTRA_ZEPHYR_MODULES` / `ZEPHYR_EXTRA_MODULES` route is only needed for a
module path outside the manifest.

### 2.3 Verify that the module was found

```sh
west list | grep nanomodbus
grep CONFIG_NANOMODBUS build/zephyr/.config
```

If `CONFIG_NANOMODBUS` is not visible in menuconfig, Zephyr did not discover
the module. Check the path, `zephyr/module.yml`, and the build variable above.

Do **not** also add `nanomodbus.c` to your application sources when using the
module: the module already compiles the core, and adding it again produces
duplicate symbols.

## 3. Enable features in `prj.conf`

`CONFIG_NANOMODBUS` is disabled by default. The client and server roles are
enabled by default once the module itself is on; the serial transport is on by
default and TCP is off.

A client-only application should set `CONFIG_NANOMODBUS_SERVER=n`: it removes
the server role from the core and saves flash as well as RAM. A server-only
application can likewise set `CONFIG_NANOMODBUS_CLIENT=n` to drop the
mutex-wrapped client wrapper.

### RTU client

```conf
CONFIG_NANOMODBUS=y
CONFIG_NANOMODBUS_CLIENT=y
CONFIG_NANOMODBUS_TRANSPORT_SERIAL=y
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
```

### RTU server

```conf
CONFIG_NANOMODBUS=y
CONFIG_NANOMODBUS_SERVER=y
CONFIG_NANOMODBUS_SERVER_RTU=y
CONFIG_NANOMODBUS_TRANSPORT_SERIAL=y
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
```

### TCP client

```conf
CONFIG_NANOMODBUS=y
CONFIG_NANOMODBUS_CLIENT=y
CONFIG_NANOMODBUS_TRANSPORT_TCP=y
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_IPV4=y
```

### TCP server

```conf
CONFIG_NANOMODBUS=y
CONFIG_NANOMODBUS_SERVER=y
CONFIG_NANOMODBUS_SERVER_TCP=y
CONFIG_NANOMODBUS_TRANSPORT_TCP=y
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_IPV4=y
```

### Public headers

```c
#include <zephyr/nanomodbus/nanomodbus_z.h>          /* client + common */
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>   /* RTU transport/server */
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>      /* TCP transport/server */
```

`<nanomodbus.h>` is also on the include path for code that uses the portable
core directly.

## 4. Devicetree

### 4.1 UART

Give the Modbus UART an alias and make sure it is enabled:

```dts
/ {
    aliases {
        modbus-uart = &uart2;
    };
};

&uart2 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart2_tx_pd5 &uart2_rx_pd6>;
    pinctrl-names = "default";
};
```

The pinctrl labels are board/SoC specific. Do not share the Modbus UART with the
console: IRQ and asynchronous receive modes take exclusive ownership of the
device.

### 4.2 RS-485 driver enable

`NMBS_Z_SERIAL_DE_AUTO` uses hardware DE when the UART reports RS-485 flow
control, otherwise the GPIO from `de_gpio` if one is provided. For hardware DE,
enable it in devicetree. On STM32:

```dts
&uart2 {
    de-enable;
    de-assert-time = <0>;      /* 16ths of a bit time */
    de-deassert-time = <0>;
};
```

The DE pin must also be muxed through pinctrl. Other SoCs expose their own
binding properties; check the UART binding for your SoC.

For a GPIO-controlled transceiver:

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
    aliases {
        modbus-de = &modbus_de;
    };

    modbus_de: modbus-de {
        gpios = <&gpiod 4 GPIO_ACTIVE_HIGH>;
    };
};
```

```c
config.de_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(modbus_de), gpios);
config.de_mode = NMBS_Z_SERIAL_DE_GPIO;   /* or NMBS_Z_SERIAL_DE_AUTO */
```

`NMBS_Z_SERIAL_DE_NONE` is the right choice for a full-duplex link.

### 4.3 Ethernet

Enable the MAC node in the board overlay:

```dts
&mac {
    status = "okay";
};
```

and in the board configuration:

```conf
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_DHCPV4=y
```

## 5. RTU client

The client wrapper serializes calls with a mutex and copies the
`nmbs_platform_conf` it is given. The transport adapter stored a pointer to the
transport in that configuration, so the transport must outlive the client too.
Keep both in static storage (or any storage that outlives the calls).

```c
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/sys/printk.h>

static nmbs_z_serial_transport_t transport;
static nmbs_z_client_t client;

int main(void)
{
    const struct device *uart = DEVICE_DT_GET(DT_ALIAS(modbus_uart));

    if (!device_is_ready(uart)) {
        return -ENODEV;
    }

    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = uart,
        .baud_rate = 115200,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .de_mode = NMBS_Z_SERIAL_DE_NONE,
        .rx_mode = NMBS_Z_SERIAL_RX_IRQ,
    };

    nmbs_platform_conf platform;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &platform);
    if (ret != 0) {
        return ret;
    }

    ret = nmbs_z_client_init(&client, &platform);
    if (ret != 0) {
        return ret;
    }

    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    if (ret != 0) {
        return ret;
    }

    uint16_t regs[4];

    while (1) {
        ret = nmbs_z_client_read_holding_registers(&client, 0, 4, regs, K_MSEC(500));
        if (ret == 0) {
            printk("%u %u %u %u\n", regs[0], regs[1], regs[2], regs[3]);
        } else {
            printk("read failed: %d (nmbs_error %d)\n",
                   ret, nmbs_z_client_get_last_error(&client));
        }

        k_sleep(K_SECONDS(1));
    }
}
```

Notes:

- `nmbs_z_client_set_target_address()` selects the RTU slave address and also
  takes a lock timeout.
- The `K_MSEC(500)` argument of the request helpers is the **mutex acquisition**
  timeout. The response and inter-byte timeouts come from
  `CONFIG_NANOMODBUS_CLIENT_DEFAULT_READ_TIMEOUT_MS` /
  `CONFIG_NANOMODBUS_CLIENT_DEFAULT_BYTE_TIMEOUT_MS`, or from
  `nmbs_z_client_set_timeouts(&client, read_ms, byte_ms)`.
- `nmbs_z_client_get_last_error()` returns the raw `nmbs_error`. Use it to tell
  a Modbus exception (1..4) from a transport error such as
  `NMBS_ERROR_TIMEOUT`.
- `rx_mode` must match your Kconfig: `NMBS_Z_SERIAL_RX_IRQ` needs
  `CONFIG_UART_INTERRUPT_DRIVEN`; `NMBS_Z_SERIAL_RX_ASYNC` needs
  `CONFIG_UART_ASYNC_API` and DMA.

## 6. RTU server

The RTU server is event driven: a `k_timer` measures the t3.5 silence interval
and a per-instance workqueue processes the request. The instance embeds its
workqueue stack, so it must be zero-initialized static storage.

```c
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

static uint16_t holding[16];

static nmbs_error read_holding_registers(uint16_t address, uint16_t quantity,
                                         uint16_t *registers_out, uint8_t unit_id,
                                         void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if ((uint32_t)address + quantity > ARRAY_SIZE(holding)) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = holding[address + i];
    }

    return NMBS_ERROR_NONE;
}

static const nmbs_callbacks callbacks = {
    .read_holding_registers = read_holding_registers,
};

static nmbs_z_server_rtu_t server;

int main(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(holding); i++) {
        holding[i] = (uint16_t)(0x1000 + i);
    }

    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_DT_GET(DT_ALIAS(modbus_uart)),
        .baud_rate = 115200,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .de_mode = NMBS_Z_SERIAL_DE_AUTO,
        .rx_mode = NMBS_Z_SERIAL_RX_IRQ,
    };

    int ret = nmbs_z_server_rtu_init(&server, &cfg, &callbacks, 1 /* slave address */);
    if (ret != 0) {
        return ret;
    }

    ret = nmbs_z_server_rtu_start(&server);
    if (ret != 0) {
        return ret;
    }

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
```

Notes:

- The slave address must be 1..247; otherwise `nmbs_z_server_rtu_init()`
  returns `-EINVAL`.
- `nmbs_z_server_rtu_init()` copies the callbacks into the instance. To pass a
  context to them, call `nmbs_set_callbacks_arg(&server.core, ctx)` after
  initialization.
- In IRQ and ASYNC modes the server installs its own UART callback. Do not
  register another callback on the same UART, and do not feed bytes yourself.
- In `NMBS_Z_SERIAL_RX_EXTERNAL` mode the application owns reception and must
  feed bytes with `nmbs_z_server_rtu_rx_byte()` (or
  `nmbs_z_server_rtu_rx_data()`), for example from a polling loop or a custom
  ISR.
- The workqueue priority must be preemptible. The default (5) is.
- Use `nmbs_z_server_rtu_stop()` to stop accepting requests; the workqueue
  thread stays alive so the server can be started again.

## 7. TCP client

```c
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/sys/printk.h>

static nmbs_z_tcp_transport_t transport;
static nmbs_z_client_t client;

int main(void)
{
    nmbs_platform_conf platform;
    int ret = nmbs_z_tcp_transport_connect(&transport, "192.168.1.50", 502,
                                           5000, &platform);
    if (ret != 0) {
        return ret;
    }

    ret = nmbs_z_client_init(&client, &platform);
    if (ret != 0) {
        nmbs_z_tcp_transport_close(&transport);
        return ret;
    }

    uint16_t regs[4];

    ret = nmbs_z_client_read_holding_registers(&client, 0, 4, regs, K_MSEC(1000));
    if (ret != 0) {
        printk("read failed: %d (nmbs_error %d)\n",
               ret, nmbs_z_client_get_last_error(&client));
    }

    nmbs_z_tcp_transport_close(&transport);
    return 0;
}
```

Notes:

- The address is the **server** address. A loopback address such as
  `127.0.0.1` points at the board itself and will not work.
- Bring the network interface up and wait for a DHCP lease before connecting:

```c
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>

struct net_if *iface = net_if_get_default();

net_dhcpv4_start(iface);

while (net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) == NULL) {
    k_sleep(K_MSEC(500));
}
```

  Avoid `CONFIG_NET_CONFIG_NEED_IPV4=y` in an application that must start
  quickly: that option blocks `main()` for up to 30 seconds waiting for the
  lease.
- `CONFIG_NANOMODBUS_CLIENT_TCP_TIMEOUT_MS` covers the TCP handshake and the
  socket send/receive watchdog.
- On a poll failure, close the transport and call
  `nmbs_z_tcp_transport_connect()` again. A connected socket can be closed with
  `nmbs_z_tcp_transport_close()`.
- `CONFIG_NANOMODBUS_TCP_KEEPALIVE=y` (requires `CONFIG_NET_TCP_KEEPALIVE=y`)
  makes a silently disappeared peer surface as a transport error.

## 8. TCP server

The TCP server instance embeds its listener thread and stack, so it must live in
static storage. It serves exactly one client at a time; further connections
stay in the listen backlog until the current one closes.

```c
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>

static uint16_t holding[16];

static nmbs_error read_holding_registers(uint16_t address, uint16_t quantity,
                                         uint16_t *registers_out, uint8_t unit_id,
                                         void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if ((uint32_t)address + quantity > ARRAY_SIZE(holding)) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = holding[address + i];
    }

    return NMBS_ERROR_NONE;
}

static const nmbs_callbacks callbacks = {
    .read_holding_registers = read_holding_registers,
};

static nmbs_z_server_tcp_t server;

int main(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(holding); i++) {
        holding[i] = (uint16_t)(0x1000 + i);
    }

    int ret = nmbs_z_server_tcp_init(&server, &callbacks, 502);
    if (ret != 0) {
        return ret;
    }

    ret = nmbs_z_server_tcp_start(&server, NULL, 0);
    if (ret != 0) {
        return ret;
    }

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
```

`nmbs_z_server_tcp_start(&server, &callbacks, 502)` performs initialization and
start in one call. Use `nmbs_z_server_tcp_stop()` to stop the listener.

Notes:

- Callbacks run on the server thread. Keep them short, or hand work to another
  thread.
- The Modbus unit ID is carried in the MBAP header. Most TCP clients send 0;
  use the value only if you need it to route between units.
- To serve several clients concurrently, instantiate one server per port. Each
  instance owns its own core, transport and stack.
- The listener binds to all interfaces; bring Ethernet up and optionally log the
  DHCP address for convenience.

## 9. RS-485 and receive modes

### Driver-enable modes

| `de_mode` | Behavior |
| --- | --- |
| `NMBS_Z_SERIAL_DE_AUTO` | Hardware DE if the UART reports RS-485 flow control, else the configured GPIO DE, else none |
| `NMBS_Z_SERIAL_DE_HARDWARE` | Hardware DE only; fails with `-ENOTSUP` if the UART is not configured for it |
| `NMBS_Z_SERIAL_DE_GPIO` | Software DE through `de_gpio`; fails with `-EINVAL` if no GPIO was provided |
| `NMBS_Z_SERIAL_DE_NONE` | Full-duplex link, no DE control |

The GPIO mode uses `de_pre_delay_us` and `de_post_delay_us` as guard times.

When `CONFIG_UART_INTERRUPT_DRIVEN=y`, the transport waits for UART TX completion
before releasing DE. Without it, it waits only one character time, so DE may
drop while bytes are still queued. Enable interrupt-driven UART for RS-485.

### Receive modes

| `rx_mode` | Requirements | Notes |
| --- | --- | --- |
| `NMBS_Z_SERIAL_RX_EXTERNAL` | None | The application feeds received bytes itself |
| `NMBS_Z_SERIAL_RX_IRQ` | `CONFIG_UART_INTERRUPT_DRIVEN=y` | The transport drains the UART FIFO from its own callback |
| `NMBS_Z_SERIAL_RX_ASYNC` | `CONFIG_UART_ASYNC_API=y` and DMA in devicetree | Uses two alternating buffers; recovers from RX errors through a work item |

IRQ and ASYNC modes require exclusive ownership of the UART instance.

The asynchronous buffers live inside `nmbs_z_serial_transport_t` in `.bss`. On a
cache-enabled MCU (for example STM32H7 with `CONFIG_DCACHE=y`), DMA-capable UART
drivers may reject them with `-EFAULT`. Either disable the data cache for the
application or place the transport in a non-cacheable region. The sample
`server_rtu` disables `CONFIG_DCACHE` for this reason.

## 10. Memory and threading

There is no heap allocation. Measured on Cortex-M7 with Zephyr 4.4 and the
default Kconfig:

| Object | Approximate RAM | Notes |
| --- | --- | --- |
| `nmbs_z_client_t` | 412 B | 380 B with `CONFIG_NANOMODBUS_SERVER=n` |
| `nmbs_z_serial_transport_t` | ~640 B | Includes the RX ring buffer and async buffers |
| `nmbs_z_tcp_transport_t` | 36 B | Socket state only |
| `nmbs_z_server_rtu_t` | ~2.4 KB | Includes the workqueue thread stack (1 KB default) |
| `nmbs_z_server_tcp_t` | ~2.8 KB | Includes the listener thread stack (2 KB default) |

Sizes grow with the configurable stack and buffer sizes.

- `nmbs_z_client_t` is safe to share between threads: every request takes the
  internal mutex. The lock timeout is the last argument of each helper.
- `nmbs_z_client_get_last_error()` is a per-client value, not per-call. Read it
  under the same serialization as the request.
- RTU server callbacks run on the per-instance workqueue; TCP server callbacks
  run on the per-instance listener thread.
- Server objects embed kernel stacks: never put them on the stack of `main()` or
  another thread. Use static storage.
- Large client state (transport + client) should also be static when `main()`
  runs on a small stack. A TCP client with ~500 B of locals can overflow the
  default 1 KB main stack once the socket call chain is added; the `client_tcp`
  sample keeps them static and sets `CONFIG_MAIN_STACK_SIZE=2048`.

## 11. Samples

All samples target `nucleo_h743zi` (STM32H743ZI). Other boards need their own
overlay and board configuration.

| Sample | Role / transport | What it does |
| --- | --- | --- |
| `samples/client_rtu` | RTU client | Polls 4 holding registers and writes a heartbeat register every second |
| `samples/client_tcp` | TCP client | Takes DHCP, then polls 4 holding registers from a configurable server |
| `samples/server_rtu` | RTU server | Serves 16 holding registers and 16 coils; IRQ or async RX |
| `samples/server_tcp` | TCP server | Serves registers/coils on port 502 and logs its DHCP address |
| `samples/modbus_gateway` | RTU clients + TCP server | Publishes two RTU buses through a 32-register TCP cache |

Build a sample:

```sh
west build -p always -b nucleo_h743zi samples/client_rtu -d build_client_rtu \
  -- -DEXTRA_ZEPHYR_MODULES=$PWD
```

The sample `CMakeLists.txt` files already append the repository as an extra
module, so `-DEXTRA_ZEPHYR_MODULES=$PWD` is optional inside this repository. It
is required if you copy a sample somewhere else.

`client_tcp` needs the address and port of a Modbus TCP server reachable from
the board. Override the sample options at build time:

```sh
west build -p always -b nucleo_h743zi samples/client_tcp -d build_client_tcp \
  -- -DEXTRA_ZEPHYR_MODULES=$PWD \
     -DCONFIG_CLIENT_TCP_SERVER_IP='"192.168.1.50"' \
     -DCONFIG_CLIENT_TCP_SERVER_PORT=502
```

The defaults (`192.168.1.10:502`) are documented in
`samples/client_tcp/Kconfig`.

## 12. Tests

Host tests for the portable core:

```sh
cmake -S . -B build_host -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host -j
ctest --test-dir build_host --output-on-failure
```

`BUILD_TESTS` and `BUILD_EXAMPLES` default to OFF, so a bare `cmake ..` only
builds the library. Some host tests use real sleeps, which is why the full
`ctest` run takes about 70 seconds.

Zephyr tests (native_sim):

```sh
west twister -T tests/zephyr -p native_sim --extra-args ZEPHYR_MODULES=$PWD
```

To run a single suite without Twister:

```sh
west build -p always -b native_sim tests/zephyr -d build_ztest \
  -- -DEXTRA_ZEPHYR_MODULES=$PWD
./build_ztest/zephyr/zephyr.exe -list
./build_ztest/zephyr/zephyr.exe -test=nanomodbus_error::*
```

All `tests/zephyr/src/*.c` files are linked into one test binary. Add new files
to `tests/zephyr/CMakeLists.txt`; suite names differ from file names, so use
`-list` to discover them.

## 13. Kconfig reference

Run `west build -t menuconfig` or open `zephyr/Kconfig` for the authoritative
list and help text.

| Option | Default | Notes |
| --- | --- | --- |
| `NANOMODBUS` | n | Master switch; nothing is built without it |
| `NANOMODBUS_CLIENT` | y | Modbus client role and wrapper |
| `NANOMODBUS_SERVER` | y | Modbus server role |
| `NANOMODBUS_TRANSPORT_SERIAL` | y | RTU over Zephyr UART; depends on `SERIAL` |
| `NANOMODBUS_TRANSPORT_TCP` | n | TCP over BSD sockets; depends on `NET_SOCKETS` and `NET_TCP` |
| `NANOMODBUS_SERVER_RTU` | y | RTU server engine; needs server + serial |
| `NANOMODBUS_SERVER_TCP` | n | TCP server engine; needs server + TCP |
| `NANOMODBUS_CLIENT_DEFAULT_READ_TIMEOUT_MS` | 1000 | First response byte timeout |
| `NANOMODBUS_CLIENT_DEFAULT_BYTE_TIMEOUT_MS` | 100 | Inter-byte timeout |
| `NANOMODBUS_CLIENT_TCP_TIMEOUT_MS` | 5000 | TCP handshake and socket watchdog |
| `NANOMODBUS_SERIAL_RX_BUF_SIZE` | 260 | RTU receive ring buffer (256..1024) |
| `NANOMODBUS_SERIAL_ASYNC_RX_BUF_SIZE` | 128 | Each of the two async RX buffers |
| `NANOMODBUS_SERVER_RTU_BYTE_TIMEOUT_MS` | 100 | RTU server first-byte and inter-byte timeout |
| `NANOMODBUS_SERVER_RTU_WQ_STACK_SIZE` | 1024 | Per-instance workqueue stack |
| `NANOMODBUS_SERVER_RTU_WQ_PRIORITY` | 5 | Must be preemptible |
| `NANOMODBUS_SERVER_TCP_STACK_SIZE` | 2048 | Per-instance listener stack |
| `NANOMODBUS_SERVER_TCP_PRIORITY` | 7 | Listener thread priority |
| `NANOMODBUS_SERVER_TCP_TIMEOUT_MS` | 200 | Idle poll and inter-byte timeout |
| `NANOMODBUS_SERVER_TCP_BACKLOG` | 4 | Pending connections (still one served at a time) |
| `NANOMODBUS_TCP_KEEPALIVE` | n | Needs `NET_TCP_KEEPALIVE`; see the idle/interval/count options |
| `NANOMODBUS_BITFIELD_MAX` | 2000 | Coils/discrete inputs per operation; multiple of 8 |
| `NANOMODBUS_DEBUG` | n | Enables `NMBS_DEBUG` core traffic prints |

### Function-code pruning

The `NANOMODBUS_FC_*_DISABLED` options remove server-side handlers. They also
define the matching `NMBS_CLIENT_*_DISABLED` macros, but the upstream core only
honors the server-side ones: the client API is not reduced. File record (20/21),
read/write multiple registers (23) and device identification (43/14) are
disabled by default; the remaining function codes are enabled.

## 14. Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `CONFIG_NANOMODBUS` is not in menuconfig | Module not discovered | Check `west list`, `zephyr/module.yml`, and `EXTRA_ZEPHYR_MODULES` |
| `undefined reference to nmbs_z_*` | Module found but disabled | Set `CONFIG_NANOMODBUS=y` and enable the role/transport |
| `nmbs_z_serial_transport_init()` returns `-ENODEV` | UART missing or not ready | Set `status = "okay"`, check pinctrl and `device_is_ready()` |
| `-EINVAL` from serial init | `baud_rate == 0`, or IRQ/ASYNC with a NULL UART | Fix the configuration |
| `-ENOTSUP` from serial init | `DE_HARDWARE` requested without `de-enable` | Enable hardware DE or use `DE_AUTO`/`DE_GPIO` |
| RTU server never replies | Wrong slave address, no DE, or UART owned elsewhere | Check address 1..247, DE mode, and that no other callback is installed |
| Async RX init returns `-EFAULT` | STM32 DMA rejecting cacheable buffers | Disable `CONFIG_DCACHE` or use non-cacheable RAM |
| TCP connect returns `-116` (`ETIMEDOUT`) | No lease/route, wrong address, or firewall | Wait for DHCP, check the server address and port |
| TCP server accepts only one client | By design | Use one server instance per port for concurrency |
| Stack overflow or corrupted state | Large client objects on a small stack | Put transports/clients in static storage or raise the stack |
| RS-485 DE drops before the last byte | TX completion not awaited | Set `CONFIG_UART_INTERRUPT_DRIVEN=y` |
| Boot stalls ~30 s | `CONFIG_NET_CONFIG_NEED_IPV4=y` | Start DHCP explicitly and wait for the address in the application |

## 15. See also

- `samples/client_rtu`, `samples/client_tcp`, `samples/server_rtu`,
  `samples/server_tcp`, `samples/modbus_gateway`
- `zephyr/Kconfig` for the full option list and help text
- `zephyr/include/zephyr/nanomodbus/` for the public API
- Upstream API reference:
  <https://debevv.github.io/nanoMODBUS/nanomodbus_8h.html>
