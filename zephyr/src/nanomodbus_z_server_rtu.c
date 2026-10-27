/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/logging/log.h>
#include <string.h>

#ifdef CONFIG_NANOMODBUS_SERVER_RTU

LOG_MODULE_REGISTER(nmbs_server_rtu, CONFIG_NANOMODBUS_LOG_LEVEL);

#define NMBS_Z_SERVER_RTU_INIT_MAGIC 0x4E4D4253 /* 'NMBS' */

static const struct k_work_queue_config rtu_work_q_config = {
    .name = "nmbs_rtu_wq",
};

static void on_silence_timer_expired(struct k_timer *timer)
{
    nmbs_z_server_rtu_t *server = CONTAINER_OF(timer, nmbs_z_server_rtu_t, silence_timer);

    if (!atomic_get(&server->running)) {
        return;
    }

    /* A t3.5 expiry always means "a frame may be complete". If the work item is
     * still queued or running, the extra poll it performs anyway will pick the
     * bytes up, so a 0 return is not an error.
     */
    int ret = k_work_submit_to_queue(&server->work_q, &server->work);

    if (ret < 0) {
        LOG_WRN("Failed to submit RTU request work (%d)", ret);
    }
}

static int32_t server_rtu_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    nmbs_z_serial_transport_t *transport = (nmbs_z_serial_transport_t *)arg;
    int32_t ret = nmbs_z_transport_serial_write(buf, count, byte_timeout_ms, transport);
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
    if (ret > 0) {
        nmbs_z_server_rtu_t *server = CONTAINER_OF(transport, nmbs_z_server_rtu_t, transport);
        server->stats.tx_frames++;
        if (count >= 2 && (buf[1] & 0x80)) {
            server->stats.exceptions++;
        }
    }
#endif
    return ret;
}

static void on_server_work(struct k_work *work)
{
    nmbs_z_server_rtu_t *server = CONTAINER_OF(work, nmbs_z_server_rtu_t, work);

    if (!atomic_get(&server->running)) {
        return;
    }

    atomic_set(&server->rx_pending, 0);
    nmbs_error err = nmbs_server_poll(&server->core);
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
    if (err == NMBS_ERROR_CRC) {
        server->stats.crc_errors++;
    } else if (err == NMBS_ERROR_TIMEOUT) {
        server->stats.timeouts++;
    } else if (err == NMBS_ERROR_NONE) {
        if (server->core.msg.buf_idx > 0) {
            server->stats.rx_frames++;
        }
    }
#endif

    /* If more bytes arrived while the request was being handled, run again
     * instead of waiting for the next t3.5 expiry.
     */
    if (atomic_get(&server->rx_pending) && atomic_get(&server->running)) {
        int ret = k_work_submit_to_queue(&server->work_q, &server->work);

        if (ret < 0) {
            LOG_WRN("Failed to resubmit RTU request work (%d)", ret);
        }
    }
}

static void on_transport_rx(nmbs_z_serial_transport_t *transport, const uint8_t *data, size_t len,
                            void *user_data)
{
    ARG_UNUSED(transport);
    ARG_UNUSED(data);
    ARG_UNUSED(len);

    nmbs_z_server_rtu_t *server = (nmbs_z_server_rtu_t *)user_data;

    if (server == NULL || !atomic_get(&server->running)) {
        return;
    }

    atomic_set(&server->rx_pending, 1);
    k_timer_start(&server->silence_timer, K_USEC(server->transport.t3_5_us), K_NO_WAIT);
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

    if (atomic_get(&server->init_magic) == NMBS_Z_SERVER_RTU_INIT_MAGIC) {
        return -EALREADY;
    }

    memset(server, 0, sizeof(*server));
    server->server_address = server_address;

    /* Install the RTU receive callback only after the timer and core exist.
     * The transport is therefore initialized in EXTERNAL mode first.
     */
    nmbs_z_serial_transport_config_t local_cfg = *config;

    local_cfg.rx_mode = NMBS_Z_SERIAL_RX_EXTERNAL;
    local_cfg.rx_cb = NULL;
    local_cfg.rx_cb_user_data = NULL;

    nmbs_platform_conf platform_conf;
    nmbs_platform_conf_create(&platform_conf);

    int ret = nmbs_z_serial_transport_init(&server->transport, &local_cfg, &platform_conf);
    if (ret != 0) {
        return ret;
    }

    platform_conf.write = server_rtu_write;

    /* Store callbacks and guarantee initialization marker */
    server->callbacks = *callbacks;
    server->callbacks.initialized = 0xFFFFDEBE;

    nmbs_error err = nmbs_server_create(&server->core, server_address, &platform_conf, &server->callbacks);
    if (err != NMBS_ERROR_NONE) {
        nmbs_z_serial_transport_deinit(&server->transport);
        return nmbs_z_error_to_errno(err);
    }

    /* Configure receive timeouts for assembled packet parsing */
    nmbs_set_read_timeout(&server->core, CONFIG_NANOMODBUS_SERVER_RTU_BYTE_TIMEOUT_MS);
    nmbs_set_byte_timeout(&server->core, CONFIG_NANOMODBUS_SERVER_RTU_BYTE_TIMEOUT_MS);

    k_timer_init(&server->silence_timer, on_silence_timer_expired, NULL);
    k_work_init(&server->work, on_server_work);
    k_work_queue_start(&server->work_q, server->work_q_stack, K_KERNEL_STACK_SIZEOF(server->work_q_stack),
                       CONFIG_NANOMODBUS_SERVER_RTU_WQ_PRIORITY, &rtu_work_q_config);

    atomic_set(&server->running, 0);
    atomic_set(&server->rx_pending, 0);
    server->transport.cfg.rx_mode = config->rx_mode;
    server->transport.cfg.rx_cb = on_transport_rx;
    server->transport.cfg.rx_cb_user_data = server;
    atomic_set(&server->init_magic, NMBS_Z_SERVER_RTU_INIT_MAGIC);

    ret = nmbs_z_server_rtu_start(server);
    if (ret != 0) {
        atomic_set(&server->running, 0);
        return ret;
    }

    return 0;
}

int nmbs_z_server_rtu_start(nmbs_z_server_rtu_t *server)
{
    if (!server) {
        return -EINVAL;
    }

    if (atomic_get(&server->init_magic) != NMBS_Z_SERVER_RTU_INIT_MAGIC) {
        return -EINVAL;
    }

    atomic_set(&server->running, 1);

    int ret = nmbs_z_serial_transport_start_rx(&server->transport);
    if (ret != 0) {
        atomic_set(&server->running, 0);
        return ret;
    }

    return 0;
}

int nmbs_z_server_rtu_stop(nmbs_z_server_rtu_t *server)
{
    if (!server) {
        return -EINVAL;
    }

    atomic_set(&server->running, 0);
    k_timer_stop(&server->silence_timer);

    struct k_work_sync sync;
    (void)k_work_cancel_sync(&server->work, &sync);

    (void)nmbs_z_serial_transport_stop_rx(&server->transport);
    nmbs_z_transport_serial_flush(NULL, &server->transport);

    return 0;
}

void nmbs_z_server_rtu_rx_data(nmbs_z_server_rtu_t *server, const uint8_t *data, uint32_t length)
{
    if (server == NULL || data == NULL || !atomic_get(&server->running)) {
        return;
    }

    (void)nmbs_z_serial_transport_rx_put(&server->transport, data, length);
}

void nmbs_z_server_rtu_rx_byte(nmbs_z_server_rtu_t *server, uint8_t byte)
{
    nmbs_z_server_rtu_rx_data(server, &byte, 1);
}

int nmbs_z_server_rtu_get_stats(const nmbs_z_server_rtu_t *server, struct nmbs_z_stats *stats)
{
    if (!server || !stats) {
        return -EINVAL;
    }
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
    *stats = server->stats;
    return 0;
#else
    return -ENOTSUP;
#endif
}

int nmbs_z_server_rtu_reset_stats(nmbs_z_server_rtu_t *server)
{
    if (!server) {
        return -EINVAL;
    }
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
    memset(&server->stats, 0, sizeof(server->stats));
    return 0;
#else
    return -ENOTSUP;
#endif
}

#endif /* CONFIG_NANOMODBUS_SERVER_RTU */
