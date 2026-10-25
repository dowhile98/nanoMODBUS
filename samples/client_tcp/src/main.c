/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>

LOG_MODULE_REGISTER(client_tcp_app, LOG_LEVEL_INF);

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 502
#define POLL_INTERVAL_MS 1000
#define READ_QUANTITY 4

int main(void)
{
    LOG_INF("nanoMODBUS TCP Client Sample Application Starting...");

    nmbs_z_tcp_transport_t transport;
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    uint16_t registers[READ_QUANTITY];
    bool connected = false;

    while (1) {
        if (!connected) {
            LOG_INF("Connecting to Modbus TCP Server at %s:%u...", SERVER_IP, SERVER_PORT);
            int ret = nmbs_z_tcp_transport_connect(&transport, SERVER_IP, SERVER_PORT, 2000, &platform);
            if (ret != 0) {
                LOG_WRN("Connection failed (%d). Retrying in 2 seconds...", ret);
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

            LOG_INF("Connected successfully to Modbus TCP Server");
            connected = true;
        }

        /* Poll holding registers */
        int ret = nmbs_z_client_read_holding_registers(&client, 0, READ_QUANTITY, registers, K_MSEC(1000));
        if (ret == 0) {
            LOG_INF("Polled Holding Registers [0..%u]: %u, %u, %u, %u",
                    READ_QUANTITY - 1, registers[0], registers[1], registers[2], registers[3]);
        } else {
            LOG_WRN("Polling failed (%d). Reconnecting...", ret);
            nmbs_z_tcp_transport_close(&transport);
            connected = false;
        }

        k_sleep(K_MSEC(POLL_INTERVAL_MS));
    }

    return 0;
}
