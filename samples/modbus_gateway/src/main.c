/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Multi-instance Modbus gateway sample.
 *
 * Two RTU buses are polled as Modbus clients and their registers are published
 * through a Modbus TCP server. Buses whose UART is unavailable fall back to
 * simulated telemetry so the TCP side can still be exercised.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#ifdef CONFIG_NANOMODBUS_SHELL
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>
#endif

LOG_MODULE_REGISTER(modbus_gateway, CONFIG_NANOMODBUS_LOG_LEVEL);

#define GATEWAY_TCP_PORT 502

#define CACHE_REG_COUNT 32
#define BUS1_REG_OFFSET 0
#define BUS1_REG_COUNT 16
#define BUS2_REG_OFFSET 16
#define BUS2_REG_COUNT 16

/* Mutex-protected shared register cache */
static struct k_mutex cache_lock;
static uint16_t cached_holding_registers[CACHE_REG_COUNT];

/* Modbus TCP Server instance (embeds its listener stack, so static storage) */
static nmbs_z_server_tcp_t tcp_server;

/* RTU Clients for Bus 1 and Bus 2 */
static nmbs_z_client_t client_rtu1;
static nmbs_z_serial_transport_t transport_rtu1;
static nmbs_platform_conf platform_rtu1;
static bool bus1_hw_ready;

static nmbs_z_client_t client_rtu2;
static nmbs_z_serial_transport_t transport_rtu2;
static nmbs_platform_conf platform_rtu2;
static bool bus2_hw_ready;

/* Worker threads for RTU polling */
static K_THREAD_STACK_DEFINE(bus1_stack, 2048);
static K_THREAD_STACK_DEFINE(bus2_stack, 2048);
static struct k_thread bus1_thread;
static struct k_thread bus2_thread;

/* UART peripherals come from the devicetree nodes/aliases provided by the board
 * overlay. There is deliberately no fallback to the console UART: taking it
 * over would silently kill logging on boards without the aliases.
 */
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(modbus0))
#define BUS1_NODE DT_NODELABEL(modbus0)
#elif DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(modbus_bus1))
#define BUS1_NODE DT_ALIAS(modbus_bus1)
#else
#define BUS1_NODE DT_INVALID_NODE
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(modbus1))
#define BUS2_NODE DT_NODELABEL(modbus1)
#elif DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(modbus_bus2))
#define BUS2_NODE DT_ALIAS(modbus_bus2)
#else
#define BUS2_NODE DT_INVALID_NODE
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(bus1_slave))
#define BUS1_SLAVE_ADDR NMBS_Z_DT_SLAVE_ID(DT_NODELABEL(bus1_slave))
#else
#define BUS1_SLAVE_ADDR 1
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(bus2_slave))
#define BUS2_SLAVE_ADDR NMBS_Z_DT_SLAVE_ID(DT_NODELABEL(bus2_slave))
#else
#define BUS2_SLAVE_ADDR 2
#endif

/* Modbus TCP Server Callbacks serving cached RTU registers */
static nmbs_error gateway_read_holding_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out,
                                                 uint8_t unit_id, void* arg) {
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

static nmbs_error gateway_write_single_register(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
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

static const nmbs_callbacks gateway_callbacks = {
        .read_holding_registers = gateway_read_holding_registers,
        .write_single_register = gateway_write_single_register,
};

/*
 * Initialize one RTU client. Returns true when the bus can really be polled;
 * false means the caller should publish simulated values instead.
 */
static bool setup_rtu_bus(nmbs_z_client_t* client, nmbs_z_serial_transport_t* transport, nmbs_platform_conf* platform,
                          const nmbs_z_serial_transport_config_t* config, uint8_t address, const char* name) {
    if (config == NULL || config->uart_dev == NULL || !device_is_ready(config->uart_dev)) {
        LOG_WRN("%s: UART not available, publishing simulated values", name);
        return false;
    }

    int ret = nmbs_z_serial_transport_init(transport, config, platform);
    if (ret != 0) {
        LOG_ERR("%s: serial transport init failed: %d", name, ret);
        return false;
    }

    ret = nmbs_z_client_init(client, platform);
    if (ret != 0) {
        LOG_ERR("%s: client init failed: %d", name, ret);
        return false;
    }

    ret = nmbs_z_client_set_target_address(client, address, K_MSEC(100));
    if (ret != 0) {
        LOG_ERR("%s: cannot set RTU slave address %u: %d", name, address, ret);
        return false;
    }

    LOG_INF("%s: RTU client for slave %u ready", name, address);
    return true;
}

/* Polling thread for RTU Bus 1 */
static void bus1_poll_fn(void* arg1, void* arg2, void* arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uint16_t counter = 0;

    LOG_INF("RTU Bus 1 polling thread running (hardware=%s)...", bus1_hw_ready ? "active" : "simulated");

    while (1) {
        if (bus1_hw_ready) {
            uint16_t read_buf[BUS1_REG_COUNT] = {0};
            int ret = nmbs_z_client_read_holding_registers(&client_rtu1, 0, BUS1_REG_COUNT, read_buf, K_MSEC(500));
            if (ret == 0) {
                k_mutex_lock(&cache_lock, K_FOREVER);
                for (int i = 0; i < BUS1_REG_COUNT; i++) {
                    cached_holding_registers[BUS1_REG_OFFSET + i] = read_buf[i];
                }
                k_mutex_unlock(&cache_lock);
            }
            else {
                LOG_DBG("RTU Bus 1 poll failed: %d", ret);
            }
        }
        else {
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
static void bus2_poll_fn(void* arg1, void* arg2, void* arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uint16_t counter = 0;

    LOG_INF("RTU Bus 2 polling thread running (hardware=%s)...", bus2_hw_ready ? "active" : "simulated");

    while (1) {
        if (bus2_hw_ready) {
            uint16_t read_buf[BUS2_REG_COUNT] = {0};
            int ret = nmbs_z_client_read_holding_registers(&client_rtu2, 0, BUS2_REG_COUNT, read_buf, K_MSEC(500));
            if (ret == 0) {
                k_mutex_lock(&cache_lock, K_FOREVER);
                for (int i = 0; i < BUS2_REG_COUNT; i++) {
                    cached_holding_registers[BUS2_REG_OFFSET + i] = read_buf[i];
                }
                k_mutex_unlock(&cache_lock);
            }
            else {
                LOG_DBG("RTU Bus 2 poll failed: %d", ret);
            }
        }
        else {
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

static void log_ipv4_address(struct net_if* iface) {
    struct net_in_addr* addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
    char buf[NET_IPV4_ADDR_LEN];

    if (addr == NULL || net_addr_ntop(NET_AF_INET, addr, buf, sizeof(buf)) == NULL) {
        return;
    }

    LOG_INF("Gateway Modbus TCP server reachable on %s:%u", buf, GATEWAY_TCP_PORT);
}

int main(void) {
    LOG_INF("=== nanoMODBUS Multi-Instance Gateway Starting ===");

    k_mutex_init(&cache_lock);

    /* Initialize register cache values */
    for (int i = 0; i < BUS1_REG_COUNT; i++) {
        cached_holding_registers[BUS1_REG_OFFSET + i] = (uint16_t) (1000 + i);
    }
    for (int i = 0; i < BUS2_REG_COUNT; i++) {
        cached_holding_registers[BUS2_REG_OFFSET + i] = (uint16_t) (2000 + i);
    }

    /* 1. Configure and initialize RTU Client 1 (Bus 1) */
#if DT_NODE_EXISTS(BUS1_NODE)
    nmbs_z_serial_transport_config_t bus1_cfg = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(BUS1_NODE);
#else
    nmbs_z_serial_transport_config_t bus1_cfg = {0};
#endif
    bus1_hw_ready =
            setup_rtu_bus(&client_rtu1, &transport_rtu1, &platform_rtu1, &bus1_cfg, BUS1_SLAVE_ADDR, "RTU Bus 1");
#ifdef CONFIG_NANOMODBUS_SHELL
    if (bus1_hw_ready) {
        (void) nmbs_z_shell_register_client("rtu1", &client_rtu1);
    }
#endif

    /* 2. Configure and initialize RTU Client 2 (Bus 2) */
#if DT_NODE_EXISTS(BUS2_NODE)
    nmbs_z_serial_transport_config_t bus2_cfg = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(BUS2_NODE);
#else
    nmbs_z_serial_transport_config_t bus2_cfg = {0};
#endif
    bus2_hw_ready =
            setup_rtu_bus(&client_rtu2, &transport_rtu2, &platform_rtu2, &bus2_cfg, BUS2_SLAVE_ADDR, "RTU Bus 2");
#ifdef CONFIG_NANOMODBUS_SHELL
    if (bus2_hw_ready) {
        (void) nmbs_z_shell_register_client("rtu2", &client_rtu2);
    }
#endif

    /* 3. Start Modbus TCP Server serving cached registers */
    int ret = nmbs_z_server_tcp_start(&tcp_server, &gateway_callbacks, GATEWAY_TCP_PORT);
    if (ret != 0) {
        LOG_ERR("Failed to start Gateway TCP Server on port %u: %d", GATEWAY_TCP_PORT, ret);
        return ret;
    }
#ifdef CONFIG_NANOMODBUS_SHELL
    (void) nmbs_z_shell_register_server_tcp("tcp0", &tcp_server);
#endif
    LOG_INF("Gateway Modbus TCP Server listening on port %u", GATEWAY_TCP_PORT);

    /* 4. Launch polling threads for Bus 1 and Bus 2 */
    k_thread_create(&bus1_thread, bus1_stack, K_THREAD_STACK_SIZEOF(bus1_stack), bus1_poll_fn, NULL, NULL, NULL, 7, 0,
                    K_NO_WAIT);
    k_thread_name_set(&bus1_thread, "modbus_bus1");

    k_thread_create(&bus2_thread, bus2_stack, K_THREAD_STACK_SIZEOF(bus2_stack), bus2_poll_fn, NULL, NULL, NULL, 7, 0,
                    K_NO_WAIT);
    k_thread_name_set(&bus2_thread, "modbus_bus2");

    /* 5. Bring Ethernet up. The server listens on all interfaces, so the
     * lease only affects reachability, not startup.
     */
    struct net_if* iface = net_if_get_default();

    if (iface == NULL) {
        LOG_ERR("No network interface available");
        return -ENODEV;
    }

    net_dhcpv4_start(iface);

    bool logged_address = false;

    LOG_INF("nanoMODBUS Multi-Instance Gateway running successfully.");

    /* Main monitor loop */
    while (1) {
        if (!logged_address && net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL) {
            log_ipv4_address(iface);
            logged_address = true;
        }

        k_sleep(K_SECONDS(10));
        k_mutex_lock(&cache_lock, K_FOREVER);
        LOG_INF("[Gateway Telemetry] Cache Bus1[0]=%u, Bus2[0]=%u", cached_holding_registers[BUS1_REG_OFFSET],
                cached_holding_registers[BUS2_REG_OFFSET]);
        k_mutex_unlock(&cache_lock);
    }

    return 0;
}
