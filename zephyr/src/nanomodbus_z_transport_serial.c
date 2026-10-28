/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/time_units.h>
#include <string.h>

LOG_MODULE_REGISTER(nmbs_serial, CONFIG_NANOMODBUS_LOG_LEVEL);

#define NMBS_SERIAL_CHAR_BITS 11U

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

/*
 * Apply the requested line settings to the UART, leaving the flow-control
 * mode untouched. On STM32 (and any driver following the same convention)
 * UART_CFG_FLOW_CTRL_RS485 is what keeps the hardware driver-enable
 * (de-enable) active, so overwriting it would silently disable RS-485 DE.
 */
static int serial_apply_line_config(nmbs_z_serial_transport_t *transport)
{
    const struct device *dev = transport->cfg.uart_dev;

    if (dev == NULL) {
        return 0;
    }

    struct uart_config uart_cfg;
    int ret = uart_config_get(dev, &uart_cfg);

    if (ret != 0) {
        /* Runtime reconfiguration is optional; the devicetree settings then
         * stay in force.
         */
        LOG_DBG("UART line configuration not applied (%d), keeping devicetree settings", ret);
        return 0;
    }

    /* Nothing to change? Then avoid touching a possibly busy peripheral. */
    if (uart_cfg.baudrate == transport->cfg.baud_rate &&
        uart_cfg.parity == (enum uart_config_parity)transport->cfg.parity &&
        uart_cfg.stop_bits == (enum uart_config_stop_bits)transport->cfg.stop_bits &&
        uart_cfg.data_bits == (enum uart_config_data_bits)transport->cfg.data_bits) {
        return 0;
    }

    uart_cfg.baudrate = transport->cfg.baud_rate;
    uart_cfg.parity = (enum uart_config_parity)transport->cfg.parity;
    uart_cfg.stop_bits = (enum uart_config_stop_bits)transport->cfg.stop_bits;
    uart_cfg.data_bits = (enum uart_config_data_bits)transport->cfg.data_bits;

    ret = uart_configure(dev, &uart_cfg);
    if (ret == -ENOSYS || ret == -ENOTSUP) {
        LOG_DBG("UART driver cannot change line settings (%d), keeping devicetree settings", ret);
        return 0;
    }
    if (ret != 0) {
        LOG_ERR("Failed to configure UART (%d)", ret);
        return ret;
    }

    LOG_DBG("UART configured: %u bps, parity %u, %u stop bits, %u data bits", transport->cfg.baud_rate,
            transport->cfg.parity, transport->cfg.stop_bits, transport->cfg.data_bits);

    return 0;
}

static int serial_resolve_de(nmbs_z_serial_transport_t *transport)
{
    const nmbs_z_serial_transport_config_t *config = &transport->cfg;
    bool hw_de = false;
    bool gpio_de = false;

    if (config->de_mode == NMBS_Z_SERIAL_DE_NONE) {
        return 0;
    }

    if (config->de_mode == NMBS_Z_SERIAL_DE_AUTO || config->de_mode == NMBS_Z_SERIAL_DE_HARDWARE) {
        if (config->uart_dev != NULL) {
            struct uart_config uart_cfg;
            int ret = uart_config_get(config->uart_dev, &uart_cfg);

            if (ret == 0) {
                hw_de = (uart_cfg.flow_ctrl == UART_CFG_FLOW_CTRL_RS485);
            } else if (config->de_mode == NMBS_Z_SERIAL_DE_HARDWARE) {
                /* Runtime configuration queries are optional. When the
                 * driver cannot report its configuration, trust that the
                 * devicetree enabled hardware DE.
                 */
                hw_de = true;
            }
        }

        if (config->de_mode == NMBS_Z_SERIAL_DE_HARDWARE && !hw_de) {
            LOG_ERR("Hardware RS-485 DE requested but UART is not configured for it");
            return -ENOTSUP;
        }
    }

    if (config->de_mode == NMBS_Z_SERIAL_DE_GPIO) {
        if (config->de_gpio.port == NULL) {
            LOG_ERR("GPIO RS-485 DE requested without a DE GPIO");
            return -EINVAL;
        }
        gpio_de = true;
    } else if (config->de_mode == NMBS_Z_SERIAL_DE_AUTO && !hw_de && config->de_gpio.port != NULL) {
        gpio_de = true;
    }

    if (gpio_de) {
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

    transport->de_hw = hw_de;
    transport->de_gpio = gpio_de;

    if (hw_de) {
        LOG_DBG("Using UART hardware RS-485 driver-enable");
    } else {
        if (gpio_de) {
            LOG_DBG("Using GPIO RS-485 driver-enable");
        }
        if (config->re_gpio.port != NULL) {
            if (!device_is_ready(config->re_gpio.port)) {
                LOG_ERR("RS-485 RE GPIO port not ready");
                return -ENODEV;
            }

            int ret = gpio_pin_configure_dt(&config->re_gpio, GPIO_OUTPUT_ACTIVE);
            if (ret < 0) {
                LOG_ERR("Failed to configure RS-485 RE pin (%d)", ret);
                return ret;
            }

            transport->re_gpio = true;
            LOG_DBG("Using GPIO RS-485 receiver-enable");
        }
    }

    return 0;
}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
static void serial_irq_cb(const struct device *dev, void *user_data)
{
    nmbs_z_serial_transport_t *transport = (nmbs_z_serial_transport_t *)user_data;
    uint8_t chunk[32];

    uart_irq_update(dev);

    while (uart_irq_rx_ready(dev) > 0) {
        int read = uart_fifo_read(dev, chunk, sizeof(chunk));

        if (read <= 0) {
            break;
        }

        (void)nmbs_z_serial_transport_rx_put(transport, chunk, (uint32_t)read);
    }
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static void serial_async_rearm(nmbs_z_serial_transport_t *transport)
{
    if (!atomic_get(&transport->rx_active)) {
        return;
    }

    transport->async_next_buf = 1;

    int ret = uart_rx_enable(transport->cfg.uart_dev, transport->async_rx_buffer[0],
                             sizeof(transport->async_rx_buffer[0]), (int32_t)transport->t1_5_us);
    if (ret == 0) {
        return;
    }

    if (ret == -EFAULT) {
        /* STM32 DMA rejects cacheable buffers; the message must name the fix
         * because the error alone is indistinguishable from a bad pointer.
         */
        LOG_ERR("Async RX buffer is not in non-cacheable memory; place the UART "
                "buffers in a nocache region / disable CONFIG_DCACHE");
    } else {
        LOG_ERR("Failed to enable asynchronous UART RX (%d)", ret);
    }

    atomic_set(&transport->rx_active, 0);
}

/*
 * A UART RX error stops the receiver, and the driver must release the DMA
 * channel (uart_rx_disable()) before reception can be enabled again. That
 * cannot be done from the event callback, which runs in ISR context, so the
 * recovery is deferred to the system workqueue. The resulting UART_RX_DISABLED
 * event re-arms reception.
 */
static void serial_async_restart_work_handler(struct k_work *work)
{
    nmbs_z_serial_transport_t *transport =
        CONTAINER_OF(work, nmbs_z_serial_transport_t, rx_restart_work);

    if (!atomic_get(&transport->rx_active)) {
        return;
    }

    int ret = uart_rx_disable(transport->cfg.uart_dev);

    /* -EFAULT means the driver already considers RX disabled but still emits
     * UART_RX_DISABLED, which performs the re-arm. Any other failure has no
     * guaranteed event, so re-arm explicitly.
     */
    if (ret != 0 && ret != -EFAULT) {
        LOG_WRN("UART RX disable before restart failed (%d), re-enabling directly", ret);
        serial_async_rearm(transport);
    }
}

static void serial_async_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    nmbs_z_serial_transport_t *transport = (nmbs_z_serial_transport_t *)user_data;

    switch (evt->type) {
    case UART_RX_RDY:
        if (atomic_get(&transport->rx_active) && evt->data.rx.len > 0) {
            (void)nmbs_z_serial_transport_rx_put(transport, evt->data.rx.buf + evt->data.rx.offset,
                                                 evt->data.rx.len);
        }
        break;

    case UART_RX_BUF_REQUEST: {
        uint8_t idx = transport->async_next_buf;
        int ret = uart_rx_buf_rsp(dev, transport->async_rx_buffer[idx],
                                  sizeof(transport->async_rx_buffer[idx]));

        if (ret == 0) {
            transport->async_next_buf = idx ? 0 : 1;
        } else {
            LOG_WRN("Failed to provide UART RX buffer (%d)", ret);
        }
        break;
    }

    case UART_RX_BUF_RELEASED:
        break;

    case UART_RX_DISABLED:
        if (atomic_get(&transport->rx_active)) {
            serial_async_rearm(transport);
        }
        break;

    case UART_RX_STOPPED:
        LOG_WRN("UART RX stopped (reason 0x%x), scheduling restart", evt->data.rx_stop.reason);
        if (atomic_get(&transport->rx_active)) {
            (void)k_work_submit(&transport->rx_restart_work);
        }
        break;

    default:
        break;
    }
}

int nmbs_z_serial_transport_start_rx(nmbs_z_serial_transport_t *transport)
{
    if (!transport || transport->cfg.uart_dev == NULL) {
        return -EINVAL;
    }

    if (atomic_get(&transport->rx_active)) {
        return 0;
    }

    if (transport->cfg.rx_mode == NMBS_Z_SERIAL_RX_EXTERNAL) {
        return 0;
    }

    atomic_set(&transport->rx_active, 1);

    if (transport->cfg.rx_mode == NMBS_Z_SERIAL_RX_IRQ) {
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
        int ret = uart_irq_callback_user_data_set(transport->cfg.uart_dev, serial_irq_cb, transport);

        if (ret != 0) {
            atomic_set(&transport->rx_active, 0);
            LOG_ERR("Failed to install UART IRQ callback (%d)", ret);
            return ret;
        }

        uart_irq_rx_enable(transport->cfg.uart_dev);
        return 0;
#else
        atomic_set(&transport->rx_active, 0);
        LOG_ERR("IRQ receive mode requires CONFIG_UART_INTERRUPT_DRIVEN");
        return -ENOTSUP;
#endif
    }

    if (transport->cfg.rx_mode == NMBS_Z_SERIAL_RX_ASYNC) {
        int ret = uart_callback_set(transport->cfg.uart_dev, serial_async_cb, transport);

        if (ret != 0) {
            atomic_set(&transport->rx_active, 0);
            LOG_ERR("Failed to install UART async callback (%d)", ret);
            return ret;
        }

        serial_async_rearm(transport);
        if (!atomic_get(&transport->rx_active)) {
            return -EIO;
        }
        return 0;
    }

    atomic_set(&transport->rx_active, 0);
    return -EINVAL;
}

int nmbs_z_serial_transport_stop_rx(nmbs_z_serial_transport_t *transport)
{
    if (!transport) {
        return -EINVAL;
    }

    if (!atomic_cas(&transport->rx_active, 1, 0)) {
        return 0;
    }

    /* Drop a pending error-recovery restart so it cannot disable a receiver
     * that is about to be started again.
     */
    if (k_is_in_isr()) {
        (void)k_work_cancel(&transport->rx_restart_work);
    } else {
        struct k_work_sync sync;

        (void)k_work_cancel_sync(&transport->rx_restart_work, &sync);
    }

    if (transport->cfg.uart_dev == NULL) {
        return 0;
    }

    if (transport->cfg.rx_mode == NMBS_Z_SERIAL_RX_IRQ) {
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
        uart_irq_rx_disable(transport->cfg.uart_dev);
#endif
    } else if (transport->cfg.rx_mode == NMBS_Z_SERIAL_RX_ASYNC) {
        (void)uart_rx_disable(transport->cfg.uart_dev);
    }

    return 0;
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

    if (config->rx_mode == NMBS_Z_SERIAL_RX_IRQ || config->rx_mode == NMBS_Z_SERIAL_RX_ASYNC) {
        if (config->uart_dev == NULL) {
            return -EINVAL;
        }
    }

    memset(transport, 0, sizeof(*transport));
    transport->cfg = *config;
    if (transport->cfg.stop_bits == 0) {
        transport->cfg.stop_bits = UART_CFG_STOP_BITS_1;
    }
    if (transport->cfg.data_bits == 0) {
        transport->cfg.data_bits = UART_CFG_DATA_BITS_8;
    }

    nmbs_z_serial_calc_timings(config->baud_rate, &transport->t1_5_us, &transport->t3_5_us);

    ring_buf_init(&transport->rx_rb, sizeof(transport->rx_buffer), transport->rx_buffer);
    k_sem_init(&transport->rx_sem, 0, K_SEM_MAX_LIMIT);
    k_work_init(&transport->rx_restart_work, serial_async_restart_work_handler);
    atomic_set(&transport->rx_active, 0);
    atomic_set(&transport->rx_overflow, 0);
    atomic_set(&transport->rx_resync_count, 0);
    transport->last_rx_cycles = k_cycle_get_32();

    int ret = 0;

    if (transport->cfg.uart_dev != NULL) {
        ret = serial_apply_line_config(transport);
        if (ret != 0) {
            return ret;
        }
    }

    ret = serial_resolve_de(transport);
    if (ret != 0) {
        return ret;
    }

    nmbs_platform_conf_create(platform_conf);
    platform_conf->transport = NMBS_TRANSPORT_RTU;
    platform_conf->read = nmbs_z_transport_serial_read;
    platform_conf->write = nmbs_z_transport_serial_write;
    platform_conf->flush = nmbs_z_transport_serial_flush;
    platform_conf->arg = transport;

    if (transport->cfg.rx_mode != NMBS_Z_SERIAL_RX_EXTERNAL) {
        ret = nmbs_z_serial_transport_start_rx(transport);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

void nmbs_z_serial_transport_deinit(nmbs_z_serial_transport_t *transport)
{
    if (!transport) {
        return;
    }

    (void)nmbs_z_serial_transport_stop_rx(transport);

    k_spinlock_key_t key = k_spin_lock(&transport->rb_lock);

    ring_buf_reset(&transport->rx_rb);
    k_spin_unlock(&transport->rb_lock, key);
    k_sem_reset(&transport->rx_sem);
    atomic_set(&transport->rx_overflow, 0);

    if (transport->de_gpio) {
        (void)gpio_pin_set_dt(&transport->cfg.de_gpio, 0);
    }
    if (transport->re_gpio) {
        (void)gpio_pin_set_dt(&transport->cfg.re_gpio, 1);
    }
}

int nmbs_z_serial_transport_reconfigure(nmbs_z_serial_transport_t *transport,
                                        const struct uart_config *uart_cfg)
{
    if (!transport) {
        return -EINVAL;
    }

    if (uart_cfg != NULL) {
        if (uart_cfg->baudrate == 0) {
            return -EINVAL;
        }

        transport->cfg.baud_rate = uart_cfg->baudrate;
        transport->cfg.parity = (uint8_t)uart_cfg->parity;
        transport->cfg.stop_bits = (uint8_t)uart_cfg->stop_bits;
        transport->cfg.data_bits = (uint8_t)uart_cfg->data_bits;
    }

    bool was_active = (atomic_get(&transport->rx_active) != 0);

    if (was_active) {
        (void)nmbs_z_serial_transport_stop_rx(transport);
    }

    int ret = 0;
    if (transport->cfg.uart_dev != NULL) {
        ret = serial_apply_line_config(transport);
        if (ret != 0) {
            if (was_active) {
                (void)nmbs_z_serial_transport_start_rx(transport);
            }
            return ret;
        }
    }

    nmbs_z_serial_calc_timings(transport->cfg.baud_rate, &transport->t1_5_us, &transport->t3_5_us);

    nmbs_z_transport_serial_flush(NULL, transport);

    if (was_active) {
        ret = nmbs_z_serial_transport_start_rx(transport);
        if (ret != 0) {
            return ret;
        }
    }

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
    bool gpio_de = transport->de_gpio;
    bool gpio_re = transport->re_gpio;

    /* 1. Disable RX (if RE GPIO present) and assert RS-485 DE pin (hardware DE is driven by the UART itself). */
    if (gpio_re) {
        gpio_pin_set_dt(&transport->cfg.re_gpio, 0);
    }

    if (gpio_de) {
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

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
        uint32_t wait_us = 0;
        const uint32_t max_wait_us = 50000; /* 50 ms timeout safeguard */
        while (uart_irq_tx_complete(transport->cfg.uart_dev) == 0 && wait_us < max_wait_us) {
            uart_irq_update(transport->cfg.uart_dev);
            k_busy_wait(10);
            wait_us += 10;
        }
#endif
    }

    /* 3. Wait for the shift register to drain before releasing DE. */
    if (transport->cfg.uart_dev != NULL && transport->cfg.baud_rate > 0) {
        uint32_t char_time_us = (uint32_t)((NMBS_SERIAL_CHAR_BITS * 1000000UL) / transport->cfg.baud_rate);

        if (char_time_us > 0) {
            k_busy_wait(char_time_us);
        }
    }

    if (gpio_de) {
        if (transport->cfg.de_post_delay_us > 0) {
            k_busy_wait(transport->cfg.de_post_delay_us);
        }
        gpio_pin_set_dt(&transport->cfg.de_gpio, 0);
    }

    if (gpio_re) {
        gpio_pin_set_dt(&transport->cfg.re_gpio, 1);
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
        if (atomic_cas(&transport->rx_overflow, 1, 0)) {
            k_spinlock_key_t key = k_spin_lock(&transport->rb_lock);

            ring_buf_reset(&transport->rx_rb);
            k_spin_unlock(&transport->rb_lock, key);
            k_sem_reset(&transport->rx_sem);
            return -EOVERFLOW;
        }

        k_spinlock_key_t key = k_spin_lock(&transport->rb_lock);
        uint32_t got = ring_buf_get(&transport->rx_rb, &buf[bytes_read], count - bytes_read);

        k_spin_unlock(&transport->rb_lock, key);

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
    k_spinlock_key_t key = k_spin_lock(&transport->rb_lock);

    ring_buf_reset(&transport->rx_rb);
    k_spin_unlock(&transport->rb_lock, key);

    /* k_sem_reset()/k_sem_give() are kept outside rb_lock: they may reschedule
     * and must never be called with a spinlock held. The reader always
     * re-checks the ring before sleeping, so a lost wakeup cannot lose data.
     */
    k_sem_reset(&transport->rx_sem);
    atomic_set(&transport->rx_overflow, 0);

    /* Only the external/polling path can safely drain the hardware FIFO.
     * IRQ and ASYNC modes own the RX FIFO through their callbacks.
     */
    if (transport->cfg.uart_dev != NULL && transport->cfg.rx_mode == NMBS_Z_SERIAL_RX_EXTERNAL) {
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

    uint32_t now = k_cycle_get_32();
    bool track_gap = (transport->cfg.rx_mode != NMBS_Z_SERIAL_RX_EXTERNAL) && transport->t1_5_us > 0;
    uint32_t elapsed_us = 0;
    bool resynced = false;
    uint32_t put;

    k_spinlock_key_t key = k_spin_lock(&transport->rb_lock);

    if (track_gap) {
        elapsed_us = k_cyc_to_us_floor32((uint32_t)(now - transport->last_rx_cycles));

        if (elapsed_us > transport->t1_5_us && ring_buf_size_get(&transport->rx_rb) > 0) {
            /* Modbus over serial line: any gap longer than 1.5 characters
             * ends the frame, so whatever is still buffered belongs to an
             * incomplete frame and must be discarded.
             */
            ring_buf_reset(&transport->rx_rb);
            atomic_inc(&transport->rx_resync_count);
            resynced = true;
        }
    }

    transport->last_rx_cycles = now;
    put = ring_buf_put(&transport->rx_rb, data, length);

    k_spin_unlock(&transport->rb_lock, key);

    if (put < length) {
        atomic_set(&transport->rx_overflow, 1);
    }

    if (put > 0) {
        k_sem_give(&transport->rx_sem);
    }

    if (resynced) {
        LOG_DBG("Discarded partial frame after %u us of silence", elapsed_us);
    }

    if (transport->cfg.rx_cb != NULL && put > 0) {
        transport->cfg.rx_cb(transport, data, put, transport->cfg.rx_cb_user_data);
    }

    return (int)put;
}
