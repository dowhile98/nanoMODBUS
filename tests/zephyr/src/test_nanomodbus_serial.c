/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#include <zephyr/ztest.h>

/* State tracking for GPIO and UART mock */
static struct {
    int de_val;
    int re_val;
    uint32_t de_set_count;
    uint32_t re_set_count;
    uint32_t bytes_written;
    bool de_asserted_during_tx;
    bool re_disabled_during_tx;
    struct uart_config uart_cfg;
    uint32_t configure_calls;
    bool rx_enabled;
    uart_irq_callback_user_data_t irq_cb;
    void *irq_cb_user_data;
} mock_re_tracker;

static void reset_mock_re_tracker(void) {
    memset(&mock_re_tracker, 0, sizeof(mock_re_tracker));
    mock_re_tracker.de_asserted_during_tx = true;
    mock_re_tracker.re_disabled_during_tx = true;
    mock_re_tracker.uart_cfg.baudrate = 115200;
    mock_re_tracker.uart_cfg.parity = UART_CFG_PARITY_NONE;
    mock_re_tracker.uart_cfg.stop_bits = UART_CFG_STOP_BITS_1;
    mock_re_tracker.uart_cfg.data_bits = UART_CFG_DATA_BITS_8;
    mock_re_tracker.uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;
}

static struct gpio_driver_config mock_re_gpio_cfg = {
        .port_pin_mask = (gpio_port_pins_t) 0xFFFFFFFF,
};
static struct gpio_driver_data mock_re_gpio_data = {
        .invert = 0,
};

static int mock_re_gpio_pin_configure(const struct device* port, gpio_pin_t pin, gpio_flags_t flags) {
    ARG_UNUSED(port);
    if (flags & GPIO_OUTPUT_INIT_HIGH) {
        if (pin == 1) {
            mock_re_tracker.de_val = 1;
        }
        else if (pin == 2) {
            mock_re_tracker.re_val = 1;
        }
    }
    else if (flags & GPIO_OUTPUT_INIT_LOW) {
        if (pin == 1) {
            mock_re_tracker.de_val = 0;
        }
        else if (pin == 2) {
            mock_re_tracker.re_val = 0;
        }
    }
    return 0;
}

static int mock_re_gpio_port_set_masked_raw(const struct device* port, gpio_port_pins_t mask, gpio_port_value_t value) {
    ARG_UNUSED(port);
    if (mask & BIT(1)) {
        mock_re_tracker.de_val = (value & BIT(1)) ? 1 : 0;
        mock_re_tracker.de_set_count++;
    }
    if (mask & BIT(2)) {
        mock_re_tracker.re_val = (value & BIT(2)) ? 1 : 0;
        mock_re_tracker.re_set_count++;
    }
    return 0;
}

static int mock_re_gpio_port_set_bits_raw(const struct device* port, gpio_port_pins_t pins) {
    return mock_re_gpio_port_set_masked_raw(port, pins, pins);
}

static int mock_re_gpio_port_clear_bits_raw(const struct device* port, gpio_port_pins_t pins) {
    return mock_re_gpio_port_set_masked_raw(port, pins, 0);
}

static DEVICE_API(gpio, mock_re_gpio_api) = {
        .pin_configure = mock_re_gpio_pin_configure,
        .port_set_masked_raw = mock_re_gpio_port_set_masked_raw,
        .port_set_bits_raw = mock_re_gpio_port_set_bits_raw,
        .port_clear_bits_raw = mock_re_gpio_port_clear_bits_raw,
};

static int mock_re_gpio_init(const struct device* dev) {
    ARG_UNUSED(dev);
    return 0;
}

DEVICE_DEFINE(mock_re_gpio_dev, "mock_re_gpio", mock_re_gpio_init, NULL, &mock_re_gpio_data, &mock_re_gpio_cfg,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mock_re_gpio_api);

static void mock_re_uart_poll_out(const struct device* dev, unsigned char out_char) {
    ARG_UNUSED(dev);
    ARG_UNUSED(out_char);
    if (mock_re_tracker.de_val != 1) {
        mock_re_tracker.de_asserted_during_tx = false;
    }
    if (mock_re_tracker.re_val != 0) {
        mock_re_tracker.re_disabled_during_tx = false;
    }
    mock_re_tracker.bytes_written++;
}

static int mock_re_uart_config_get(const struct device* dev, struct uart_config* cfg) {
    ARG_UNUSED(dev);
    *cfg = mock_re_tracker.uart_cfg;
    return 0;
}

static int mock_re_uart_configure(const struct device* dev, const struct uart_config* cfg) {
    ARG_UNUSED(dev);
    mock_re_tracker.uart_cfg = *cfg;
    mock_re_tracker.configure_calls++;
    return 0;
}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
static void mock_re_uart_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb, void *user_data) {
    ARG_UNUSED(dev);
    mock_re_tracker.irq_cb = cb;
    mock_re_tracker.irq_cb_user_data = user_data;
}

static void mock_re_uart_irq_rx_enable(const struct device *dev) {
    ARG_UNUSED(dev);
    mock_re_tracker.rx_enabled = true;
}

static void mock_re_uart_irq_rx_disable(const struct device *dev) {
    ARG_UNUSED(dev);
    mock_re_tracker.rx_enabled = false;
}
#endif

static DEVICE_API(uart, mock_re_uart_api) = {
        .poll_out = mock_re_uart_poll_out,
        .config_get = mock_re_uart_config_get,
        .configure = mock_re_uart_configure,
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
        .irq_callback_set = mock_re_uart_irq_callback_set,
        .irq_rx_enable = mock_re_uart_irq_rx_enable,
        .irq_rx_disable = mock_re_uart_irq_rx_disable,
#endif
};

static int mock_re_uart_init(const struct device* dev) {
    ARG_UNUSED(dev);
    return 0;
}

DEVICE_DEFINE(mock_re_uart_dev, "mock_re_uart", mock_re_uart_init, NULL, NULL, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mock_re_uart_api);

/* Devicetree macro tests */

ZTEST(nanomodbus_serial, test_dt_config_nanomodbus_serial) {
    nmbs_z_serial_transport_config_t cfg = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_NODELABEL(test_nmbs_node));

    zassert_equal(cfg.uart_dev, DEVICE_DT_GET(DT_NODELABEL(uart1)), "UART device handle mismatch");
    zassert_equal(cfg.baud_rate, 57600, "Baud rate mismatch");
    zassert_equal(cfg.de_pre_delay_us, 15, "DE pre delay mismatch");
    zassert_equal(cfg.de_post_delay_us, 25, "DE post delay mismatch");
    zassert_equal(cfg.de_mode, NMBS_Z_SERIAL_DE_GPIO, "DE mode mismatch");
    zassert_equal(cfg.rx_mode, NMBS_Z_SERIAL_RX_EXTERNAL, "RX mode mismatch");
    zassert_equal(cfg.de_gpio.pin, 1, "DE GPIO pin mismatch");
    zassert_equal(cfg.re_gpio.pin, 2, "RE GPIO pin mismatch");
    zassert_equal(cfg.parity, UART_CFG_PARITY_NONE, "Parity mismatch");
    zassert_equal(cfg.stop_bits, UART_CFG_STOP_BITS_1, "Stop bits mismatch");
    zassert_equal(cfg.data_bits, UART_CFG_DATA_BITS_8, "Data bits mismatch");
    zassert_is_null(cfg.rx_cb, "rx_cb must be NULL");
}

ZTEST(nanomodbus_serial, test_dt_config_auto_and_defaults) {
    nmbs_z_serial_transport_config_t cfg = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_NODELABEL(test_nmbs_auto));

    zassert_equal(cfg.uart_dev, DEVICE_DT_GET(DT_NODELABEL(uart1)), "UART device handle mismatch");
    zassert_equal(cfg.baud_rate, 115200, "Baud rate inherited from parent UART mismatch");
    zassert_equal(cfg.de_mode, NMBS_Z_SERIAL_DE_AUTO, "DE mode must default to AUTO");
    zassert_equal(cfg.de_pre_delay_us, 0, "Default DE pre delay must be 0");
    zassert_equal(cfg.de_post_delay_us, 0, "Default DE post delay must be 0");
    zassert_is_null(cfg.de_gpio.port, "DE GPIO port must be NULL when not configured");
    zassert_is_null(cfg.re_gpio.port, "RE GPIO port must be NULL when not configured");
}

ZTEST(nanomodbus_serial, test_dt_config_enum_modes) {
    nmbs_z_serial_transport_config_t cfg_hw = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_NODELABEL(test_nmbs_hw));
    zassert_equal(cfg_hw.de_mode, NMBS_Z_SERIAL_DE_HARDWARE, "HARDWARE DE mode mismatch");
    zassert_equal(cfg_hw.rx_mode, NMBS_Z_SERIAL_RX_IRQ, "IRQ RX mode mismatch");

    nmbs_z_serial_transport_config_t cfg_none = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_NODELABEL(test_nmbs_none));
    zassert_equal(cfg_none.de_mode, NMBS_Z_SERIAL_DE_NONE, "NONE DE mode mismatch");
    zassert_equal(cfg_none.rx_mode, NMBS_Z_SERIAL_RX_ASYNC, "ASYNC RX mode mismatch");
}

ZTEST(nanomodbus_serial, test_dt_config_zephyr_modbus_compat) {
    nmbs_z_serial_transport_config_t cfg = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(DT_NODELABEL(test_zephyr_modbus));

    zassert_equal(cfg.uart_dev, DEVICE_DT_GET(DT_NODELABEL(uart1)), "UART dev mismatch");
    zassert_equal(cfg.de_gpio.pin, 3, "zephyr,modbus-serial DE pin mismatch");
    zassert_equal(cfg.re_gpio.pin, 4, "zephyr,modbus-serial RE pin mismatch");
    zassert_equal(cfg.de_mode, NMBS_Z_SERIAL_DE_AUTO, "Default DE mode for zephyr,modbus-serial");
    zassert_equal(cfg.baud_rate, 115200, "Parent baud rate inherited");
}

ZTEST(nanomodbus_serial, test_dt_config_inst_macro) {
    nmbs_z_serial_transport_config_t cfg = NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT_INST(0);
    zassert_not_null(cfg.uart_dev, "Instance 0 UART device handle must not be NULL");
}

ZTEST(nanomodbus_serial, test_dt_slave_id_macro) {
    uint8_t slave_1 = NMBS_Z_DT_SLAVE_ID(DT_NODELABEL(test_slave_1));
    zassert_equal(slave_1, 1, "Slave 1 ID mismatch");

    uint8_t slave_42 = NMBS_Z_DT_SLAVE_ID(DT_NODELABEL(test_slave_42));
    zassert_equal(slave_42, 0x2a, "Slave 42 ID mismatch");
}

/* Runtime RE GPIO control tests */

ZTEST(nanomodbus_serial, test_rs485_re_gpio_runtime_control) {
    reset_mock_re_tracker();

    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
            .uart_dev = DEVICE_GET(mock_re_uart_dev),
            .de_gpio =
                    {
                            .port = DEVICE_GET(mock_re_gpio_dev),
                            .pin = 1,
                            .dt_flags = GPIO_ACTIVE_HIGH,
                    },
            .re_gpio =
                    {
                            .port = DEVICE_GET(mock_re_gpio_dev),
                            .pin = 2,
                            .dt_flags = GPIO_ACTIVE_HIGH,
                    },
            .baud_rate = 115200,
            .de_pre_delay_us = 10,
            .de_post_delay_us = 10,
            .de_mode = NMBS_Z_SERIAL_DE_GPIO,
            .rx_mode = NMBS_Z_SERIAL_RX_EXTERNAL,
    };
    nmbs_platform_conf p_conf;

    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "transport init must succeed");
    zassert_true(transport.de_gpio, "transport.de_gpio must be true");
    zassert_true(transport.re_gpio, "transport.re_gpio must be true");

    /* During init: RE pin must be configured as ACTIVE (1) so receiver is enabled */
    zassert_equal(mock_re_tracker.re_val, 1, "RE pin must be ACTIVE after init");

    /* Transmit payload */
    uint8_t payload[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    int32_t written = p_conf.write(payload, sizeof(payload), 1000, &transport);
    zassert_equal(written, sizeof(payload), "All bytes written");

    /* Verification: during transmission DE was asserted (1) and RE was disabled (0) */
    zassert_true(mock_re_tracker.de_asserted_during_tx, "DE must be high during TX");
    zassert_true(mock_re_tracker.re_disabled_during_tx, "RE must be disabled (0) during TX");

    /* After transmission finishes: DE is deasserted (0) and RE is re-enabled (1) */
    zassert_equal(mock_re_tracker.de_val, 0, "DE must be 0 after write");
    zassert_equal(mock_re_tracker.re_val, 1, "RE must be 1 after write");

    /* Deinit: RE remains or is restored to 1 */
    nmbs_z_serial_transport_deinit(&transport);
    zassert_equal(mock_re_tracker.re_val, 1, "RE must be 1 after deinit");
}

ZTEST(nanomodbus_serial, test_rs485_hw_de_ignores_re_gpio) {
    reset_mock_re_tracker();

    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
            .uart_dev = DEVICE_GET(mock_re_uart_dev),
            .de_gpio =
                    {
                            .port = DEVICE_GET(mock_re_gpio_dev),
                            .pin = 1,
                            .dt_flags = GPIO_ACTIVE_HIGH,
                    },
            .re_gpio =
                    {
                            .port = DEVICE_GET(mock_re_gpio_dev),
                            .pin = 2,
                            .dt_flags = GPIO_ACTIVE_HIGH,
                    },
            .baud_rate = 115200,
            .de_mode = NMBS_Z_SERIAL_DE_HARDWARE,
            .rx_mode = NMBS_Z_SERIAL_RX_EXTERNAL,
    };
    nmbs_platform_conf p_conf;

    /* 1. Since mock_re_uart does not report RS485 flow control, DE_HARDWARE returns -ENOTSUP */
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, -ENOTSUP, "Init with DE_HARDWARE on non-RS485 UART returns -ENOTSUP");
    zassert_false(transport.re_gpio, "transport.re_gpio must remain false");
    zassert_false(transport.de_gpio, "transport.de_gpio must remain false");

    /* 2. Pure DE_HARDWARE with UART reporting UART_CFG_FLOW_CTRL_RS485:
     * neither de_gpio nor re_gpio is touched.
     */
    reset_mock_re_tracker();
    mock_re_tracker.uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_RS485;
    mock_re_tracker.de_val = -1;
    mock_re_tracker.re_val = -1;

    ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "Init with DE_HARDWARE on RS485 UART must succeed");
    zassert_true(transport.de_hw, "transport.de_hw must be true");
    zassert_false(transport.de_gpio, "transport.de_gpio must be false in pure DE_HARDWARE");
    zassert_false(transport.re_gpio, "transport.re_gpio must be false in pure DE_HARDWARE");
    zassert_equal(mock_re_tracker.de_val, -1, "DE GPIO must not be configured in DE_HARDWARE");
    zassert_equal(mock_re_tracker.re_val, -1, "RE GPIO must not be configured in DE_HARDWARE");

    uint8_t payload[] = {0x01, 0x03};
    int32_t written = p_conf.write(payload, sizeof(payload), 1000, &transport);
    zassert_equal(written, sizeof(payload), "Write must succeed");
    zassert_equal(mock_re_tracker.de_set_count, 0, "DE GPIO must not be toggled in DE_HARDWARE");
    zassert_equal(mock_re_tracker.re_set_count, 0, "RE GPIO must not be toggled in DE_HARDWARE");
    nmbs_z_serial_transport_deinit(&transport);

    /* 3. Hybrid DE_AUTO with UART reporting UART_CFG_FLOW_CTRL_RS485 and re_gpio provided:
     * de_hw == true, de_gpio == false, re_gpio == true, and re_gpio toggles during write.
     */
    reset_mock_re_tracker();
    mock_re_tracker.uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_RS485;
    mock_re_tracker.de_val = -1;
    mock_re_tracker.re_val = -1;
    cfg.de_mode = NMBS_Z_SERIAL_DE_AUTO;
    cfg.de_pre_delay_us = 10;
    cfg.de_post_delay_us = 10;

    ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "Init with DE_AUTO on RS485 UART must succeed");
    zassert_true(transport.de_hw, "transport.de_hw must be true in hybrid DE_AUTO");
    zassert_false(transport.de_gpio, "transport.de_gpio must be false in hybrid DE_AUTO");
    zassert_true(transport.re_gpio, "transport.re_gpio must be true in hybrid DE_AUTO");
    zassert_equal(mock_re_tracker.de_val, -1, "DE GPIO must not be configured in hybrid DE_AUTO");
    zassert_equal(mock_re_tracker.re_val, 1, "RE GPIO must be configured ACTIVE in hybrid DE_AUTO");

    written = p_conf.write(payload, sizeof(payload), 1000, &transport);
    zassert_equal(written, sizeof(payload), "Write must succeed");
    zassert_equal(mock_re_tracker.de_set_count, 0, "DE GPIO must not be toggled in hybrid DE_AUTO");
    zassert_true(mock_re_tracker.re_disabled_during_tx, "RE GPIO must be disabled (0) during TX in hybrid DE_AUTO");
    zassert_equal(mock_re_tracker.re_val, 1, "RE GPIO must be restored to 1 after TX in hybrid DE_AUTO");
    zassert_equal(mock_re_tracker.re_set_count, 2, "RE GPIO must be toggled twice during write");
    nmbs_z_serial_transport_deinit(&transport);
}

ZTEST(nanomodbus_serial, test_serial_transport_reconfigure) {
    reset_mock_re_tracker();

    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_re_uart_dev),
        .baud_rate = 9600,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .de_mode = NMBS_Z_SERIAL_DE_NONE,
        .rx_mode = NMBS_Z_SERIAL_RX_EXTERNAL,
    };
    nmbs_platform_conf p_conf;

    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "transport init must succeed");

    /* Initial timing check for 9600 baud:
     * t1.5 = 16500000 / 9600 = 1718 us
     * t3.5 = 38500000 / 9600 = 4010 us
     */
    zassert_equal(transport.t1_5_us, 1718, "Initial t1.5 mismatch for 9600 bps");
    zassert_equal(transport.t3_5_us, 4010, "Initial t3.5 mismatch for 9600 bps");
    zassert_equal(transport.cfg.baud_rate, 9600, "Initial baud rate mismatch");
    zassert_equal(atomic_get(&transport.rx_active), 0, "rx_active must be 0 for EXTERNAL mode");

    /* Parameter validation */
    zassert_equal(nmbs_z_serial_transport_reconfigure(NULL, NULL), -EINVAL, "NULL transport");
    struct uart_config invalid_cfg = {
        .baudrate = 0,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
    };
    zassert_equal(nmbs_z_serial_transport_reconfigure(&transport, &invalid_cfg), -EINVAL, "Zero baudrate must fail");

    /* Reconfigure with NULL uart_cfg: keeps current line settings */
    ret = nmbs_z_serial_transport_reconfigure(&transport, NULL);
    zassert_equal(ret, 0, "NULL uart_cfg reconfigure must succeed");
    zassert_equal(transport.cfg.baud_rate, 9600, "Baudrate preserved with NULL uart_cfg");

    /* Reconfigure to 19200 bps, Even parity, 2 stop bits, 8 data bits */
    struct uart_config new_cfg_19200 = {
        .baudrate = 19200,
        .parity = UART_CFG_PARITY_EVEN,
        .stop_bits = UART_CFG_STOP_BITS_2,
        .data_bits = UART_CFG_DATA_BITS_8,
    };
    ret = nmbs_z_serial_transport_reconfigure(&transport, &new_cfg_19200);
    zassert_equal(ret, 0, "Reconfigure to 19200 must succeed");

    /* Verify line settings in transport config and mock UART */
    zassert_equal(transport.cfg.baud_rate, 19200, "Transport baud rate not updated");
    zassert_equal(transport.cfg.parity, UART_CFG_PARITY_EVEN, "Transport parity not updated");
    zassert_equal(transport.cfg.stop_bits, UART_CFG_STOP_BITS_2, "Transport stop bits not updated");
    zassert_equal(transport.cfg.data_bits, UART_CFG_DATA_BITS_8, "Transport data bits not updated");
    zassert_equal(mock_re_tracker.uart_cfg.baudrate, 19200, "Hardware UART baudrate not updated");
    zassert_equal(mock_re_tracker.uart_cfg.parity, UART_CFG_PARITY_EVEN, "Hardware UART parity not updated");
    zassert_equal(mock_re_tracker.uart_cfg.stop_bits, UART_CFG_STOP_BITS_2, "Hardware UART stop bits not updated");

    /* Verify recalculated timings for 19200 bps:
     * t1.5 = 16500000 / 19200 = 859 us
     * t3.5 = 38500000 / 19200 = 2005 us
     */
    zassert_equal(transport.t1_5_us, 859, "Recalculated t1.5 mismatch for 19200 bps");
    zassert_equal(transport.t3_5_us, 2005, "Recalculated t3.5 mismatch for 19200 bps");

    /* Verify RX state is preserved across reconfigure (inactive remains inactive) */
    zassert_equal(atomic_get(&transport.rx_active), 0, "rx_active must remain 0 when was inactive");

    /* Reconfigure to 115200 bps (> 19200 bps fixed timings) */
    struct uart_config new_cfg_115200 = {
        .baudrate = 115200,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
    };
    ret = nmbs_z_serial_transport_reconfigure(&transport, &new_cfg_115200);
    zassert_equal(ret, 0, "Reconfigure to 115200 must succeed");
    zassert_equal(transport.t1_5_us, 750, "t1.5 must be fixed at 750 us for 115200 bps");
    zassert_equal(transport.t3_5_us, 1750, "t3.5 must be fixed at 1750 us for 115200 bps");

    nmbs_z_serial_transport_deinit(&transport);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
    /* Test active RX state preservation with IRQ mode */
    reset_mock_re_tracker();
    nmbs_z_serial_transport_t irq_transport;
    nmbs_z_serial_transport_config_t irq_cfg = {
        .uart_dev = DEVICE_GET(mock_re_uart_dev),
        .baud_rate = 9600,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .de_mode = NMBS_Z_SERIAL_DE_NONE,
        .rx_mode = NMBS_Z_SERIAL_RX_IRQ,
    };

    ret = nmbs_z_serial_transport_init(&irq_transport, &irq_cfg, &p_conf);
    zassert_equal(ret, 0, "IRQ transport init must succeed");
    zassert_equal(atomic_get(&irq_transport.rx_active), 1, "rx_active must be 1 after IRQ init");
    zassert_true(mock_re_tracker.rx_enabled, "Hardware RX must be enabled");

    /* Reconfigure while RX is active */
    struct uart_config irq_new_cfg = {
        .baudrate = 57600,
        .parity = UART_CFG_PARITY_ODD,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
    };
    ret = nmbs_z_serial_transport_reconfigure(&irq_transport, &irq_new_cfg);
    zassert_equal(ret, 0, "Reconfigure active IRQ transport must succeed");

    /* Verify RX state is preserved (remains active) */
    zassert_equal(atomic_get(&irq_transport.rx_active), 1, "rx_active must remain 1 after reconfigure");
    zassert_true(mock_re_tracker.rx_enabled, "Hardware RX must still be enabled");
    zassert_equal(irq_transport.cfg.baud_rate, 57600, "Baud rate updated to 57600");
    zassert_equal(irq_transport.cfg.parity, UART_CFG_PARITY_ODD, "Parity updated to ODD");
    zassert_equal(irq_transport.t1_5_us, 750, "t1.5 must be 750 us for 57600 bps");
    zassert_equal(irq_transport.t3_5_us, 1750, "t3.5 must be 1750 us for 57600 bps");

    /* Now stop RX and reconfigure again: must preserve inactive state */
    nmbs_z_serial_transport_stop_rx(&irq_transport);
    zassert_equal(atomic_get(&irq_transport.rx_active), 0, "rx_active must be 0 after stop_rx");
    zassert_false(mock_re_tracker.rx_enabled, "Hardware RX must be disabled");

    irq_new_cfg.baudrate = 19200;
    ret = nmbs_z_serial_transport_reconfigure(&irq_transport, &irq_new_cfg);
    zassert_equal(ret, 0, "Reconfigure stopped transport must succeed");
    zassert_equal(atomic_get(&irq_transport.rx_active), 0, "rx_active must remain 0 when reconfigured while stopped");
    zassert_false(mock_re_tracker.rx_enabled, "Hardware RX must remain disabled");

    nmbs_z_serial_transport_deinit(&irq_transport);
#endif
}
