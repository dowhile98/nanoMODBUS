/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(nmbs_serial, CONFIG_NANOMODBUS_LOG_LEVEL);

void nmbs_z_serial_calc_timings(uint32_t baud_rate, uint32_t *t1_5_us, uint32_t *t3_5_us)
{
    if (!t1_5_us || !t3_5_us) {
        return;
    }

    if (baud_rate == 0) {
        *t1_5_us = 0;
        *t3_5_us = 0;
        return;
    }

    if (baud_rate <= 19200) {
        *t1_5_us = (uint32_t)(16500000UL / baud_rate);
        *t3_5_us = (uint32_t)(38500000UL / baud_rate);
    } else {
        *t1_5_us = 750;
        *t3_5_us = 1750;
    }
}

int nmbs_z_serial_transport_init(nmbs_z_serial_transport_t *transport,
                                 const nmbs_z_serial_transport_config_t *config,
                                 nmbs_platform_conf *platform_conf)
{
    if (!transport || !config || !platform_conf) {
        return -EINVAL;
    }

    if (config->baud_rate == 0) {
        return -EINVAL;
    }

    if (config->uart_dev != NULL && !device_is_ready(config->uart_dev)) {
        LOG_ERR("UART device %s not ready", config->uart_dev->name);
        return -ENODEV;
    }

    if (config->de_gpio.port != NULL) {
        if (!device_is_ready(config->de_gpio.port)) {
            LOG_ERR("RS-485 DE GPIO port not ready");
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&config->de_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure RS-485 DE pin (%d)", ret);
            return ret;
        }
    }

    transport->cfg = *config;
    nmbs_z_serial_calc_timings(config->baud_rate, &transport->t1_5_us, &transport->t3_5_us);

    transport->rx_buf = transport->rx_buffer;
    transport->rx_buf_size = sizeof(transport->rx_buffer);

    ring_buf_init(&transport->rx_rb, transport->rx_buf_size, transport->rx_buf);
    k_sem_init(&transport->rx_sem, 0, K_SEM_MAX_LIMIT);

    nmbs_platform_conf_create(platform_conf);
    platform_conf->transport = NMBS_TRANSPORT_RTU;
    platform_conf->read = nmbs_z_transport_serial_read;
    platform_conf->write = nmbs_z_transport_serial_write;
    platform_conf->flush = nmbs_z_transport_serial_flush;
    platform_conf->arg = transport;

    return 0;
}

int32_t nmbs_z_transport_serial_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);

    if (!arg || !buf) {
        return -EINVAL;
    }

    if (count == 0) {
        return 0;
    }

    nmbs_z_serial_transport_t *transport = (nmbs_z_serial_transport_t *)arg;
    bool has_de = (transport->cfg.de_gpio.port != NULL);

    /* 1. Assert RS-485 DE pin */
    if (has_de) {
        gpio_pin_set_dt(&transport->cfg.de_gpio, 1);
        if (transport->cfg.de_pre_delay_us > 0) {
            k_busy_wait(transport->cfg.de_pre_delay_us);
        }
    }

    /* 2. Push all bytes to UART */
    if (transport->cfg.uart_dev != NULL) {
        for (uint16_t i = 0; i < count; i++) {
            uart_poll_out(transport->cfg.uart_dev, buf[i]);
        }

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
        uart_irq_update(transport->cfg.uart_dev);
        while (uart_irq_tx_complete(transport->cfg.uart_dev) == 0) {
            k_busy_wait(10);
        }
#endif
    }

    /* 3. Review Focus 2: Wait for shift register drain and post delay before deasserting DE */
    if (has_de) {
        if (transport->cfg.uart_dev != NULL && transport->cfg.baud_rate > 0) {
            uint32_t char_time_us = 11000000UL / transport->cfg.baud_rate;
            if (char_time_us > 0) {
                k_busy_wait(char_time_us);
            }
        }
        if (transport->cfg.de_post_delay_us > 0) {
            k_busy_wait(transport->cfg.de_post_delay_us);
        }
        gpio_pin_set_dt(&transport->cfg.de_gpio, 0);
    }

    return (int32_t)count;
}

int32_t nmbs_z_transport_serial_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    if (!arg || !buf) {
        return -EINVAL;
    }

    if (count == 0) {
        return 0;
    }

    nmbs_z_serial_transport_t *transport = (nmbs_z_serial_transport_t *)arg;
    uint16_t bytes_read = 0;

    while (bytes_read < count) {
        uint32_t got = ring_buf_get(&transport->rx_rb, &buf[bytes_read], count - bytes_read);
        if (got > 0) {
            bytes_read += (uint16_t)got;
            continue;
        }

        if (byte_timeout_ms == 0) {
            break;
        }

        k_timeout_t timeout = (byte_timeout_ms < 0) ? K_FOREVER : K_MSEC(byte_timeout_ms);
        int sem_ret = k_sem_take(&transport->rx_sem, timeout);
        if (sem_ret != 0) {
            break;
        }
    }

    return (int32_t)bytes_read;
}

void nmbs_z_transport_serial_flush(nmbs_t *nmbs, void *arg)
{
    ARG_UNUSED(nmbs);

    if (!arg) {
        return;
    }

    nmbs_z_serial_transport_t *transport = (nmbs_z_serial_transport_t *)arg;

    ring_buf_reset(&transport->rx_rb);
    k_sem_reset(&transport->rx_sem);

    if (transport->cfg.uart_dev != NULL) {
        unsigned char dummy;
        while (uart_poll_in(transport->cfg.uart_dev, &dummy) == 0) {
            /* Drain hardware RX FIFO */
        }
    }
}

int nmbs_z_serial_transport_rx_put(nmbs_z_serial_transport_t *transport, const uint8_t *data, uint32_t length)
{
    if (!transport || !data || length == 0) {
        return 0;
    }

    uint32_t put = ring_buf_put(&transport->rx_rb, data, length);
    if (put > 0) {
        for (uint32_t i = 0; i < put; i++) {
            k_sem_give(&transport->rx_sem);
        }
    }

    return (int)put;
}
