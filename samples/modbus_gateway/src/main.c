/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>

LOG_MODULE_REGISTER(modbus_gateway, CONFIG_NANOMODBUS_LOG_LEVEL);

#define GATEWAY_TCP_PORT 502

#define CACHE_REG_COUNT 32
#define BUS1_REG_OFFSET 0
#define BUS1_REG_COUNT  16
#define BUS2_REG_OFFSET 16
#define BUS2_REG_COUNT  16

/* Mutex-protected shared register cache */
static struct k_mutex cache_lock;
static uint16_t cached_holding_registers[CACHE_REG_COUNT];

/* Modbus TCP Server instance */
static nmbs_z_server_tcp_t tcp_server;

/* RTU Clients for Bus 1 and Bus 2 */
static nmbs_z_client_t client_rtu1;
static nmbs_z_serial_transport_t transport_rtu1;

static nmbs_z_client_t client_rtu2;
static nmbs_z_serial_transport_t transport_rtu2;

/* Worker threads for RTU polling */
static K_THREAD_STACK_DEFINE(bus1_stack, 2048);
static K_THREAD_STACK_DEFINE(bus2_stack, 2048);
static struct k_thread bus1_thread;
static struct k_thread bus2_thread;

/* UART peripherals selection based on active devicetree status */
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(modbus_bus1))
#define BUS1_UART_NODE DT_ALIAS(modbus_bus1)
#elif DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(usart1))
#define BUS1_UART_NODE DT_NODELABEL(usart1)
#elif DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(zephyr_console))
#define BUS1_UART_NODE DT_CHOSEN(zephyr_console)
#else
#define BUS1_UART_NODE DT_INVALID_NODE
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(modbus_bus2))
#define BUS2_UART_NODE DT_ALIAS(modbus_bus2)
#elif DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(usart2))
#define BUS2_UART_NODE DT_NODELABEL(usart2)
#elif DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(zephyr_console))
#define BUS2_UART_NODE DT_CHOSEN(zephyr_console)
#else
#define BUS2_UART_NODE DT_INVALID_NODE
#endif

/* Modbus TCP Server Callbacks serving cached RTU registers */
static nmbs_error gateway_read_holding_registers(uint16_t address, uint16_t quantity,
                                                uint16_t *registers_out, uint8_t unit_id,
                                                void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (address + quantity > CACHE_REG_COUNT) {
        LOG_WRN("TCP Client requested out-of-bounds holding registers: addr=%u, qty=%u", address, quantity);
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    k_mutex_lock(&cache_lock, K_FOREVER);
    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = cached_holding_registers[address + i];
    }
    k_mutex_unlock(&cache_lock);

    LOG_INF("Served TCP Client: read %u holding registers starting at %u", quantity, address);
    return NMBS_ERROR_NONE;
}

static nmbs_error gateway_write_single_register(uint16_t address, uint16_t value,
                                               uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (address >= CACHE_REG_COUNT) {
        LOG_WRN("TCP Client write out-of-bounds: addr=%u", address);
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    k_mutex_lock(&cache_lock, K_FOREVER);
    cached_holding_registers[address] = value;
    k_mutex_unlock(&cache_lock);

    LOG_INF("Served TCP Client: wrote register %u = 0x%04X", address, value);
    return NMBS_ERROR_NONE;
}

/* Polling thread for RTU Bus 1 */
static void bus1_poll_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool hardware_ready = (transport_rtu1.cfg.uart_dev != NULL &&
                           device_is_ready(transport_rtu1.cfg.uart_dev));
    uint16_t counter = 0;

    LOG_INF("RTU Bus 1 polling thread running (hardware=%s)...", hardware_ready ? "active" : "simulated");

    while (1) {
        if (hardware_ready) {
            uint16_t read_buf[BUS1_REG_COUNT] = {0};
            int ret = nmbs_z_client_read_holding_registers(&client_rtu1, 0, BUS1_REG_COUNT,
                                                           read_buf, K_MSEC(500));
            if (ret == 0) {
                k_mutex_lock(&cache_lock, K_FOREVER);
                for (int i = 0; i < BUS1_REG_COUNT; i++) {
                    cached_holding_registers[BUS1_REG_OFFSET + i] = read_buf[i];
                }
                k_mutex_unlock(&cache_lock);
            } else {
                LOG_DBG("RTU Bus 1 poll failed: %d", ret);
            }
        } else {
            /* Simulated RTU Bus 1 slave telemetry */
            k_mutex_lock(&cache_lock, K_FOREVER);
            cached_holding_registers[BUS1_REG_OFFSET + 0] = 1000 + counter;
            cached_holding_registers[BUS1_REG_OFFSET + 1] = 1001;
            k_mutex_unlock(&cache_lock);
            counter++;
        }

        k_sleep(K_MSEC(500));
    }
}

/* Polling thread for RTU Bus 2 */
static void bus2_poll_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool hardware_ready = (transport_rtu2.cfg.uart_dev != NULL &&
                           device_is_ready(transport_rtu2.cfg.uart_dev));
    uint16_t counter = 0;

    LOG_INF("RTU Bus 2 polling thread running (hardware=%s)...", hardware_ready ? "active" : "simulated");

    while (1) {
        if (hardware_ready) {
            uint16_t read_buf[BUS2_REG_COUNT] = {0};
            int ret = nmbs_z_client_read_holding_registers(&client_rtu2, 0, BUS2_REG_COUNT,
                                                           read_buf, K_MSEC(500));
            if (ret == 0) {
                k_mutex_lock(&cache_lock, K_FOREVER);
                for (int i = 0; i < BUS2_REG_COUNT; i++) {
                    cached_holding_registers[BUS2_REG_OFFSET + i] = read_buf[i];
                }
                k_mutex_unlock(&cache_lock);
            } else {
                LOG_DBG("RTU Bus 2 poll failed: %d", ret);
            }
        } else {
            /* Simulated RTU Bus 2 slave telemetry */
            k_mutex_lock(&cache_lock, K_FOREVER);
            cached_holding_registers[BUS2_REG_OFFSET + 0] = 2000 + counter;
            cached_holding_registers[BUS2_REG_OFFSET + 1] = 2001;
            k_mutex_unlock(&cache_lock);
            counter++;
        }

        k_sleep(K_MSEC(500));
    }
}

int main(void)
{
    LOG_INF("=== nanoMODBUS Multi-Instance Gateway Starting ===");

    k_mutex_init(&cache_lock);

    /* Initialize register cache values */
    for (int i = 0; i < BUS1_REG_COUNT; i++) {
        cached_holding_registers[BUS1_REG_OFFSET + i] = (uint16_t)(1000 + i);
    }
    for (int i = 0; i < BUS2_REG_COUNT; i++) {
        cached_holding_registers[BUS2_REG_OFFSET + i] = (uint16_t)(2000 + i);
    }

    /* 1. Configure and initialize RTU Client 1 (Bus 1) */
    const struct device *uart1_dev = NULL;
#if DT_NODE_HAS_STATUS_OKAY(BUS1_UART_NODE)
    uart1_dev = DEVICE_DT_GET(BUS1_UART_NODE);
#endif
    nmbs_z_serial_transport_config_t cfg1 = {
        .uart_dev = (uart1_dev && device_is_ready(uart1_dev)) ? uart1_dev : NULL,
        .baud_rate = 115200,
    };
    nmbs_platform_conf plat1;
    nmbs_z_serial_transport_init(&transport_rtu1, &cfg1, &plat1);
    nmbs_z_client_init(&client_rtu1, &plat1);
    nmbs_z_client_set_target_address(&client_rtu1, 1, K_MSEC(100));

    /* 2. Configure and initialize RTU Client 2 (Bus 2) */
    const struct device *uart2_dev = NULL;
#if DT_NODE_HAS_STATUS_OKAY(BUS2_UART_NODE)
    uart2_dev = DEVICE_DT_GET(BUS2_UART_NODE);
#endif
    nmbs_z_serial_transport_config_t cfg2 = {
        .uart_dev = (uart2_dev && device_is_ready(uart2_dev)) ? uart2_dev : NULL,
        .baud_rate = 115200,
    };
    nmbs_platform_conf plat2;
    nmbs_z_serial_transport_init(&transport_rtu2, &cfg2, &plat2);
    nmbs_z_client_init(&client_rtu2, &plat2);
    nmbs_z_client_set_target_address(&client_rtu2, 2, K_MSEC(100));

    /* 3. Start Modbus TCP Server serving cached registers */
    nmbs_callbacks server_cbs = {
        .read_holding_registers = gateway_read_holding_registers,
        .write_single_register = gateway_write_single_register,
    };

    int ret = nmbs_z_server_tcp_start(&tcp_server, &server_cbs, GATEWAY_TCP_PORT);
    if (ret != 0) {
        LOG_ERR("Failed to start Gateway TCP Server on port %u: %d", GATEWAY_TCP_PORT, ret);
        return ret;
    }
    LOG_INF("Gateway Modbus TCP Server listening on port %u", GATEWAY_TCP_PORT);

    /* 4. Launch polling threads for Bus 1 and Bus 2 */
    k_thread_create(&bus1_thread, bus1_stack, K_THREAD_STACK_SIZEOF(bus1_stack),
                    bus1_poll_fn, NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);

    k_thread_create(&bus2_thread, bus2_stack, K_THREAD_STACK_SIZEOF(bus2_stack),
                    bus2_poll_fn, NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);

    LOG_INF("nanoMODBUS Multi-Instance Gateway running successfully.");

    /* Main monitor loop */
    while (1) {
        k_sleep(K_SECONDS(10));
        k_mutex_lock(&cache_lock, K_FOREVER);
        LOG_INF("[Gateway Telemetry] Cache Bus1[0]=%u, Bus2[0]=%u",
                cached_holding_registers[BUS1_REG_OFFSET],
                cached_holding_registers[BUS2_REG_OFFSET]);
        k_mutex_unlock(&cache_lock);
    }

    return 0;
}
