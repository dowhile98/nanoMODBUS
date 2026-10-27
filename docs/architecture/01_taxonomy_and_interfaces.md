# Component Classification, Hexagonal Architecture & Abstract Interfaces

## 1. Component Classification Tree

Following the Principal Embedded Software Architect taxonomy, the nanoMODBUS Zephyr ecosystem is strictly partitioned as follows:

```
├── Domain / Protocol Core (Pure ISO C99, zero OS, zero hardware dependencies)
│   ├── nanomodbus.h                     [Interface / Domain Contract]
│   └── nanomodbus.c                     [Domain / Modbus Protocol Engine]
│
├── Service / Middleware (Project-agnostic utilities & OS-independent primitives)
│   ├── zephyr/src/nanomodbus_z_common.c [POSIX errno translation service]
│   └── (Proposed) Bitfield & Register utilities
│
├── Active Objects (Dedicated OSAL thread / queue for streaming & async I/O)
│   ├── zephyr/src/nanomodbus_z_server_rtu.c [RTU Server Engine: k_timer + dedicated k_work_q]
│   └── zephyr/src/nanomodbus_z_server_tcp.c [TCP Server Engine: dedicated k_thread listener]
│
├── Adapters (Hexagonal Adapters translating OS/Hardware drivers to Core Ports)
│   ├── zephyr/src/nanomodbus_z_transport_serial.c [Serial Adapter: Zephyr UART + GPIO DE]
│   ├── zephyr/src/nanomodbus_z_transport_tcp.c    [TCP Adapter: Zephyr BSD Sockets API]
│   └── zephyr/src/nanomodbus_z_client.c           [Client Facade Adapter: Mutex-guarded synchronous caller]
│
├── Drivers (HAL Humble Objects touching hardware registers directly)
│   └── Handled exclusively by Zephyr upstream device drivers (uart_stm32, gpio_stm32, etc.)
│
├── BSP & Target Integration
│   ├── samples/*/boards/nucleo_h743zi.overlay     [Pinmux, devicetree aliases: modbus-uart, modbus-de]
│   └── samples/*/boards/nucleo_h743zi.conf        [Driver configs: UART_INTERRUPT_DRIVEN, ETH]
│
└── Interfaces (Pure Contracts & Port Headers)
    ├── zephyr/include/zephyr/nanomodbus/nanomodbus_z.h        [Core Zephyr Module API]
    ├── zephyr/include/zephyr/nanomodbus/nanomodbus_z_serial.h [Serial RTU Transport Contract]
    └── zephyr/include/zephyr/nanomodbus/nanomodbus_z_tcp.h    [TCP Transport Contract]
```

---

## 2. Ports and Adapters (Hexagonal Architecture)

The system enforces strict Dependency Inversion:

```
                         ┌─────────────────────────────────────────┐
                         │           Application Layer             │
                         │    (e.g., samples/server_rtu/main.c)    │
                         └───────────────────┬─────────────────────┘
                                             │
             ┌───────────────────────────────┴──────────────────────────────┐
             │                                                              │
             ▼ (Invokes Client)                                             ▼ (Implements Callbacks)
┌─────────────────────────┐                                    ┌─────────────────────────┐
│   nmbs_z_client_t       │                                    │   nmbs_callbacks        │
│   (Synchronous Facade)  │                                    │   (Application Port)    │
└────────────┬────────────┘                                    └────────────▲────────────┘
             │                                                              │
             │ calls                                                        │ dispatches to
             ▼                                                              │
┌───────────────────────────────────────────────────────────────────────────┴────────────┐
│                                   DOMAIN CORE (Hexagonal Core)                         │
│                                      nanomodbus.c / nanomodbus.h                       │
│                                                                                        │
│   - Frame Serializer / Deserializer                                                    │
│   - PDU Validator & Exception Generator                                                │
│   - Modbus CRC-16 Engine                                                               │
│   - State machine context: struct nmbs_t (Private members, 100% reentrant)             │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │
                                            │ calls function pointers via
                                            ▼
                               ┌─────────────────────────┐
                               │   nmbs_platform_conf    │
                               │   (Driver / I/O Port)   │
                               │   - read()              │
                               │   - write()             │
                               │   - flush()             │
                               │   - crc_calc()          │
                               └────────────▲────────────┘
                                            │
                      ┌─────────────────────┴─────────────────────┐
                      │                                           │
                      ▼                                           ▼
          ┌─────────────────────────┐                 ┌─────────────────────────┐
          │  Serial RTU Adapter     │                 │   TCP Socket Adapter    │
          │  (nanomodbus_z_serial)  │                 │   (nanomodbus_z_tcp)    │
          │                         │                 │                         │
          │  - Ring buffer (rx_rb)  │                 │  - zsock_poll / recv    │
          │  - t1.5 / t3.5 timing   │                 │  - zsock_send           │
          │  - RS-485 DE GPIO / HW  │                 │  - TCP_NODELAY tuning   │
          └───────────┬─────────────┘                 └───────────┬─────────────┘
                      │                                           │
                      ▼                                           ▼
          ┌─────────────────────────┐                 ┌─────────────────────────┐
          │   Zephyr UART Driver    │                 │   Zephyr Sockets / LwIP │
          └─────────────────────────┘                 └─────────────────────────┘
```

---

## 3. The Golden Invariant: Verification

1. **Vendor Headers in Core & Interfaces**:
   - `nanomodbus.c` and `nanomodbus.h` include **ZERO** vendor headers (`stm32*.h`, `nrf_*.h`) and **ZERO** Zephyr headers (`zephyr/*.h`).
   - `zephyr/src/*` includes only Zephyr Subsystem APIs (`<zephyr/kernel.h>`, `<zephyr/drivers/uart.h>`, `<zephyr/drivers/gpio.h>`, `<zephyr/net/socket.h>`). No SoC register definitions or CMSIS vendor headers are referenced.
   - All board and hardware specifics are decoupled into Devicetree overlays (`nucleo_h743zi.overlay`).

2. **Single Responsibility**:
   - Each source file in `zephyr/src/` maps to a distinct layer and responsibility.
   - Upstream `nanomodbus.c` concentrates protocol parsing and serialization, maintaining zero I/O side effects outside the injected `nmbs_platform_conf` function pointers.

---

## 4. Pure Contract Interface Headers

To enforce formal port contracts and allow mocking/faking in unit tests, the following interfaces define the boundary contracts:

### 4.1 Interface: I/O Transport Port (`i_modbus_transport.h`)

```c
#ifndef INTERFACES_I_MODBUS_TRANSPORT_H_
#define INTERFACES_I_MODBUS_TRANSPORT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum modbus_transport_status {
    MODBUS_TRANSPORT_OK           = 0,
    MODBUS_TRANSPORT_ERR_IO       = -1,
    MODBUS_TRANSPORT_ERR_TIMEOUT  = -2,
    MODBUS_TRANSPORT_ERR_PARAM    = -3,
    MODBUS_TRANSPORT_ERR_OVERFLOW = -4,
    MODBUS_TRANSPORT_ERR_CONN     = -5,
} modbus_transport_status_t;

typedef struct i_modbus_transport_ops {
    /**
     * @brief Read bytes from the transport medium.
     * @param buf Destination buffer.
     * @param count Exact number of bytes requested.
     * @param timeout_ms Timeout per byte or transaction in milliseconds (-1 for infinite).
     * @param ctx Opaque transport context pointer.
     * @return Number of bytes actually read (count on success), or negative error code.
     */
    int32_t (*read)(uint8_t *buf, uint16_t count, int32_t timeout_ms, void *ctx);

    /**
     * @brief Transmit bytes over the transport medium.
     * @param buf Source buffer.
     * @param count Exact number of bytes to send.
     * @param timeout_ms Timeout in milliseconds.
     * @param ctx Opaque transport context pointer.
     * @return Number of bytes sent, or negative error code.
     */
    int32_t (*write)(const uint8_t *buf, uint16_t count, int32_t timeout_ms, void *ctx);

    /**
     * @brief Flush pending unread/unwritten bytes on the medium.
     * @param ctx Opaque transport context pointer.
     */
    void (*flush)(void *ctx);
} i_modbus_transport_ops_t;

typedef struct i_modbus_transport {
    const i_modbus_transport_ops_t *ops;
    void *ctx;
} i_modbus_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* INTERFACES_I_MODBUS_TRANSPORT_H_ */
```

### 4.2 Interface: Atomic Client Request Contract (`i_modbus_client.h`)

```c
#ifndef INTERFACES_I_MODBUS_CLIENT_H_
#define INTERFACES_I_MODBUS_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include "nanomodbus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread-safe Modbus Client request contract.
 * Every operation accepts unit_id explicitly to eliminate cross-thread target corruption.
 */
typedef struct i_modbus_client_ops {
    int (*read_holding_registers)(void *client_ctx, uint8_t unit_id, uint16_t address,
                                  uint16_t quantity, uint16_t *registers_out, int32_t timeout_ms);

    int (*write_multiple_registers)(void *client_ctx, uint8_t unit_id, uint16_t address,
                                   uint16_t quantity, const uint16_t *registers, int32_t timeout_ms);

    int (*read_write_registers)(void *client_ctx, uint8_t unit_id,
                                uint16_t read_address, uint16_t read_quantity, uint16_t *registers_out,
                                uint16_t write_address, uint16_t write_quantity, const uint16_t *registers,
                                int32_t timeout_ms);
} i_modbus_client_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* INTERFACES_I_MODBUS_CLIENT_H_ */
```
