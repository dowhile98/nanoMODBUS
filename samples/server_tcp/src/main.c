/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Modbus TCP server sample.
 *
 * The board takes an address over DHCP. The sample logs it once the lease is
 * in place, which is the address to point a Modbus TCP client at (port 502).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

LOG_MODULE_REGISTER(server_tcp_app, CONFIG_NANOMODBUS_LOG_LEVEL);

#define MODBUS_TCP_PORT 502
#define HOLDING_REG_COUNT 16
#define COIL_COUNT 16

static uint16_t holding_registers[HOLDING_REG_COUNT];
static uint8_t coils[(COIL_COUNT + 7) / 8];

static nmbs_error handle_read_holding_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out,
                                                uint8_t unit_id, void* arg) {
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Read holding registers: addr=%u, qty=%u, unit_id=%u", address, quantity, unit_id);
    if (address + quantity > HOLDING_REG_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = holding_registers[address + i];
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_write_single_register(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Write single register: addr=%u, val=0x%04X, unit_id=%u", address, value, unit_id);
    if (address >= HOLDING_REG_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    holding_registers[address] = value;
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_write_multiple_registers(uint16_t address, uint16_t quantity, const uint16_t* registers,
                                                  uint8_t unit_id, void* arg) {
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Write multiple registers: addr=%u, qty=%u, unit_id=%u", address, quantity, unit_id);
    if (address + quantity > HOLDING_REG_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        holding_registers[address + i] = registers[i];
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_read_coils(uint16_t address, uint16_t quantity, nmbs_bitfield coils_out, uint8_t unit_id,
                                    void* arg) {
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Read coils: addr=%u, qty=%u, unit_id=%u", address, quantity, unit_id);
    if (address + quantity > COIL_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t idx = address + i;
        bool state = (coils[idx / 8] & (1 << (idx % 8))) != 0;
        nmbs_bitfield_write(coils_out, i, state);
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_write_single_coil(uint16_t address, bool value, uint8_t unit_id, void* arg) {
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Write single coil: addr=%u, val=%d, unit_id=%u", address, value, unit_id);
    if (address >= COIL_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    if (value) {
        coils[address / 8] |= (1 << (address % 8));
    }
    else {
        coils[address / 8] &= ~(1 << (address % 8));
    }
    return NMBS_ERROR_NONE;
}

/*
 * Log the DHCP lease once it arrives so the address to use from a Modbus TCP
 * client is visible on the console.
 */
static void log_ipv4_address(void) {
    struct net_if* iface = net_if_get_default();
    if (iface == NULL) {
        return;
    }

    struct net_in_addr* addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
    if (addr == NULL) {
        return;
    }

    char buf[NET_IPV4_ADDR_LEN];

    if (net_addr_ntop(NET_AF_INET, addr, buf, sizeof(buf)) == NULL) {
        return;
    }

    LOG_INF("Modbus TCP server listening on %s:%u", buf, MODBUS_TCP_PORT);
}

int main(void) {
    LOG_INF("nanoMODBUS TCP Server Sample Application Starting...");

    for (int i = 0; i < HOLDING_REG_COUNT; i++) {
        holding_registers[i] = (uint16_t) (0x1000 + i);
    }

    nmbs_callbacks cbs = {
            .read_holding_registers = handle_read_holding_registers,
            .write_single_register = handle_write_single_register,
            .write_multiple_registers = handle_write_multiple_registers,
            .read_coils = handle_read_coils,
            .write_single_coil = handle_write_single_coil,
    };

    /* Static storage: the instance embeds the listener thread stack. */
    static nmbs_z_server_tcp_t server;

    int ret = nmbs_z_server_tcp_init(&server, &cbs, MODBUS_TCP_PORT);
    if (ret != 0) {
        LOG_ERR("Failed to initialize TCP server: %d", ret);
        return ret;
    }

    ret = nmbs_z_server_tcp_start(&server, NULL, 0);
    if (ret != 0) {
        LOG_ERR("Failed to start TCP server: %d", ret);
        return ret;
    }

    LOG_INF("Modbus TCP server started on port %u", MODBUS_TCP_PORT);

    /* Asking for a lease before the link is up is harmless: the DHCP client
     * waits for carrier and retransmits on its own.
     */
    struct net_if* iface = net_if_get_default();

    if (iface == NULL) {
        LOG_ERR("No network interface available");
        return -ENODEV;
    }

    net_dhcpv4_start(iface);
    LOG_INF("Waiting for a DHCPv4 lease (carrier %s)", net_if_is_carrier_ok(iface) ? "up" : "down");

    bool logged_address = false;

    while (1) {
        if (!logged_address && net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL) {
            log_ipv4_address();
            logged_address = true;
        }

        k_sleep(K_SECONDS(1));
    }

    return 0;
}
