/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/logging/log.h>
#include <string.h>

#ifdef CONFIG_NANOMODBUS_SERVER_RTU

LOG_MODULE_REGISTER(nmbs_server_rtu, CONFIG_NANOMODBUS_LOG_LEVEL);

static void on_silence_timer_expired(struct k_timer *timer)
{
    nmbs_z_server_rtu_t *server = CONTAINER_OF(timer, nmbs_z_server_rtu_t, silence_timer);
    if (server->running) {
        k_work_submit(&server->work);
    }
}

static void on_server_work(struct k_work *work)
{
    nmbs_z_server_rtu_t *server = CONTAINER_OF(work, nmbs_z_server_rtu_t, work);
    if (!server->running) {
        return;
    }

    nmbs_server_poll(&server->core);
}

int nmbs_z_server_rtu_init(nmbs_z_server_rtu_t *server,
                           const nmbs_z_serial_transport_config_t *config,
                           const nmbs_callbacks *callbacks,
                           uint8_t server_address)
{
    if (!server || !config || !callbacks) {
        return -EINVAL;
    }

    if (server_address == 0 || server_address > 247) {
        return -EINVAL;
    }

    memset(server, 0, sizeof(*server));
    server->server_address = server_address;

    nmbs_platform_conf platform_conf;
    nmbs_platform_conf_create(&platform_conf);

    int ret = nmbs_z_serial_transport_init(&server->transport, config, &platform_conf);
    if (ret != 0) {
        return ret;
    }

    /* Store callbacks and guarantee initialization marker */
    server->callbacks = *callbacks;
    server->callbacks.initialized = 0xFFFFDEBE;

    nmbs_error err = nmbs_server_create(&server->core, server_address, &platform_conf, &server->callbacks);
    if (err != NMBS_ERROR_NONE) {
        return nmbs_z_error_to_errno(err);
    }

    /* Configure receive timeouts for assembled packet parsing */
    nmbs_set_read_timeout(&server->core, 100);
    nmbs_set_byte_timeout(&server->core, 100);

    k_timer_init(&server->silence_timer, on_silence_timer_expired, NULL);
    k_work_init(&server->work, on_server_work);
    server->running = true;

    return 0;
}

int nmbs_z_server_rtu_start(nmbs_z_server_rtu_t *server)
{
    if (!server) {
        return -EINVAL;
    }

    server->running = true;
    return 0;
}

int nmbs_z_server_rtu_stop(nmbs_z_server_rtu_t *server)
{
    if (!server) {
        return -EINVAL;
    }

    server->running = false;
    k_timer_stop(&server->silence_timer);

    struct k_work_sync sync;
    k_work_cancel_sync(&server->work, &sync);

    return 0;
}

void nmbs_z_server_rtu_rx_byte(nmbs_z_server_rtu_t *server, uint8_t byte)
{
    if (!server || !server->running) {
        return;
    }

    nmbs_z_serial_transport_rx_put(&server->transport, &byte, 1);
    k_timer_start(&server->silence_timer, K_USEC(server->transport.t3_5_us), K_NO_WAIT);
}

#endif /* CONFIG_NANOMODBUS_SERVER_RTU */
