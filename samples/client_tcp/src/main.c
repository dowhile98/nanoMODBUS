/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Modbus TCP client sample.
 *
 * The board takes an address over DHCP, then polls holding registers from the
 * server selected with CONFIG_CLIENT_TCP_SERVER_IP / CONFIG_CLIENT_TCP_SERVER_PORT.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(client_tcp_app, CONFIG_NANOMODBUS_LOG_LEVEL);

#define SERVER_IP CONFIG_CLIENT_TCP_SERVER_IP
#define SERVER_PORT CONFIG_CLIENT_TCP_SERVER_PORT
#define POLL_INTERVAL_MS 1000
#define READ_QUANTITY 4

/*
 * The transport, client and platform configuration are kept in static storage.
 * Together they are close to 500 bytes; as main() locals they did not fit in
 * the default 1 KB main stack once the socket call chain was added, which
 * overflowed the stack on the first connect attempt.
 */
static nmbs_z_tcp_transport_t transport;
static nmbs_z_client_t client;
static nmbs_platform_conf platform;
static uint16_t registers[READ_QUANTITY];

static void log_ipv4_address(struct net_if* iface) {
    struct net_in_addr* addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
    char buf[NET_IPV4_ADDR_LEN];

    if (addr == NULL || net_addr_ntop(NET_AF_INET, addr, buf, sizeof(buf)) == NULL) {
        return;
    }

    LOG_INF("Local address %s, connecting to %s:%d", buf, SERVER_IP, SERVER_PORT);
}

int main(void) {
    LOG_INF("nanoMODBUS TCP Client Sample Application Starting...");

    struct net_if* iface = net_if_get_default();

    if (iface == NULL) {
        LOG_ERR("No network interface available");
        return -ENODEV;
    }

    /* Request the lease explicitly instead of using CONFIG_NET_CONFIG_NEED_IPV4,
     * which blocks the whole application for up to 30 seconds at boot.
     */
    net_dhcpv4_start(iface);
    LOG_INF("Waiting for a DHCPv4 lease (carrier %s)", net_if_is_carrier_ok(iface) ? "up" : "down");

    int64_t last_wait_log = k_uptime_get();

    while (net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) == NULL) {
        if (k_uptime_get() - last_wait_log >= 5000) {
            LOG_WRN("Still waiting for a DHCPv4 lease (carrier %s)", net_if_is_carrier_ok(iface) ? "up" : "down");
            last_wait_log = k_uptime_get();
        }

        k_sleep(K_MSEC(500));
    }

    log_ipv4_address(iface);

    bool connected = false;

    while (1) {
        if (!connected) {
            int ret = nmbs_z_tcp_transport_connect(&transport, SERVER_IP, SERVER_PORT,
                                                   CONFIG_NANOMODBUS_CLIENT_TCP_TIMEOUT_MS, &platform);
            if (ret != 0) {
                LOG_WRN("Connection to %s:%d failed (%d). Retrying in 2 seconds...", SERVER_IP, SERVER_PORT, ret);
                k_sleep(K_MSEC(2000));
                continue;
            }

            ret = nmbs_z_client_init(&client, &platform);
            if (ret != 0) {
                LOG_ERR("Failed to initialize client instance: %d", ret);
                nmbs_z_tcp_transport_close(&transport);
                k_sleep(K_MSEC(2000));
                continue;
            }

            LOG_INF("Connected to Modbus TCP server at %s:%d", SERVER_IP, SERVER_PORT);
            connected = true;
        }

        /* Poll holding registers */
        int ret = nmbs_z_client_read_holding_registers(&client, 0, READ_QUANTITY, registers, K_MSEC(1000));
        if (ret == 0) {
            LOG_INF("Polled Holding Registers [0..%u]: %u, %u, %u, %u", READ_QUANTITY - 1, registers[0], registers[1],
                    registers[2], registers[3]);
        }
        else {
            LOG_WRN("Polling failed (%d). Reconnecting...", ret);
            nmbs_z_tcp_transport_close(&transport);
            connected = false;
        }

        k_sleep(K_MSEC(POLL_INTERVAL_MS));
    }

    return 0;
}
