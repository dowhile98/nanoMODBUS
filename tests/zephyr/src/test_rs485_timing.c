/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

/* Mock GPIO and UART state capture */
static struct {
    int de_state;
    uint32_t de_assert_count;
    uint32_t de_deassert_count;
    bool premature_deassert_detected;
    uint32_t bytes_written;
} mock_de_tracker;

static struct gpio_driver_config mock_gpio_cfg = {
    .port_pin_mask = (gpio_port_pins_t)0xFFFFFFFF,
};
static struct gpio_driver_data mock_gpio_data = {
    .invert = 0,
};

static int mock_gpio_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pin);
    ARG_UNUSED(flags);
    return 0;
}

static int mock_gpio_port_set_masked_raw(const struct device *port, gpio_port_pins_t mask, gpio_port_value_t value)
{
    ARG_UNUSED(port);
    ARG_UNUSED(mask);
    ARG_UNUSED(value);
    return 0;
}

static int mock_gpio_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);
    mock_de_tracker.de_state = 1;
    mock_de_tracker.de_assert_count++;
    return 0;
}

static int mock_gpio_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);
    mock_de_tracker.de_state = 0;
    mock_de_tracker.de_deassert_count++;
    return 0;
}

static DEVICE_API(gpio, mock_gpio_api) = {
    .pin_configure = mock_gpio_pin_configure,
    .port_set_masked_raw = mock_gpio_port_set_masked_raw,
    .port_set_bits_raw = mock_gpio_port_set_bits_raw,
    .port_clear_bits_raw = mock_gpio_port_clear_bits_raw,
};

static int mock_dev_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

DEVICE_DEFINE(mock_gpio_dev, "mock_gpio", mock_dev_init, NULL,
              &mock_gpio_data, &mock_gpio_cfg,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mock_gpio_api);

static void mock_uart_poll_out(const struct device *dev, unsigned char out_char)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(out_char);
    /* Review Focus 2: verify DE is held high while bytes are transmitting */
    if (mock_de_tracker.de_state != 1) {
        mock_de_tracker.premature_deassert_detected = true;
    }
    mock_de_tracker.bytes_written++;
}

static DEVICE_API(uart, mock_uart_api) = {
    .poll_out = mock_uart_poll_out,
};

DEVICE_DEFINE(mock_uart_dev, "mock_uart", mock_dev_init, NULL,
              NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mock_uart_api);

static void reset_mock_tracker(void)
{
    mock_de_tracker.de_state = 0;
    mock_de_tracker.de_assert_count = 0;
    mock_de_tracker.de_deassert_count = 0;
    mock_de_tracker.premature_deassert_detected = false;
    mock_de_tracker.bytes_written = 0;
}

/* Timing verification tests */
ZTEST(nanomodbus_serial, test_baud_silence_calculations)
{
    uint32_t t1_5_us = 0;
    uint32_t t3_5_us = 0;

    /* 9600 bps: 11 bits = 1145.8 us; t1.5 = 1718 us; t3.5 = 4010 us */
    nmbs_z_serial_calc_timings(9600, &t1_5_us, &t3_5_us);
    zassert_between_inclusive(t1_5_us, 1700, 1730, "t1.5 for 9600 bps");
    zassert_between_inclusive(t3_5_us, 4000, 4030, "t3.5 for 9600 bps");

    /* 19200 bps: 11 bits = 572.9 us; t1.5 = 859 us; t3.5 = 2005 us */
    nmbs_z_serial_calc_timings(19200, &t1_5_us, &t3_5_us);
    zassert_between_inclusive(t1_5_us, 850, 870, "t1.5 for 19200 bps");
    zassert_between_inclusive(t3_5_us, 2000, 2020, "t3.5 for 19200 bps");

    /* 38400 bps (> 19200): fixed t1.5 = 750 us, fixed t3.5 = 1750 us */
    nmbs_z_serial_calc_timings(38400, &t1_5_us, &t3_5_us);
    zassert_equal(t1_5_us, 750, "t1.5 fixed at 750 us for 38400");
    zassert_equal(t3_5_us, 1750, "t3.5 fixed at 1750 us for 38400");

    /* 115200 bps (> 19200): fixed t1.5 = 750 us, fixed t3.5 = 1750 us */
    nmbs_z_serial_calc_timings(115200, &t1_5_us, &t3_5_us);
    zassert_equal(t1_5_us, 750, "t1.5 fixed at 750 us for 115200");
    zassert_equal(t3_5_us, 1750, "t3.5 fixed at 1750 us for 115200");

    /* 0 bps edge case */
    nmbs_z_serial_calc_timings(0, &t1_5_us, &t3_5_us);
    zassert_equal(t1_5_us, 0, "t1.5 is 0 for 0 baud");
    zassert_equal(t3_5_us, 0, "t3.5 is 0 for 0 baud");
}

/* Parameter validation tests */
ZTEST(nanomodbus_serial, test_transport_init_validation)
{
    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = NULL,
        .baud_rate = 115200,
        .de_pre_delay_us = 10,
        .de_post_delay_us = 10,
    };
    nmbs_platform_conf p_conf;

    /* NULL pointers */
    zassert_equal(nmbs_z_serial_transport_init(NULL, &cfg, &p_conf), -EINVAL, "NULL transport");
    zassert_equal(nmbs_z_serial_transport_init(&transport, NULL, &p_conf), -EINVAL, "NULL config");
    zassert_equal(nmbs_z_serial_transport_init(&transport, &cfg, NULL), -EINVAL, "NULL platform_conf");

    /* Zero baud rate */
    cfg.baud_rate = 0;
    zassert_equal(nmbs_z_serial_transport_init(&transport, &cfg, &p_conf), -EINVAL, "Zero baud rate");

    /* Valid configuration */
    cfg.baud_rate = 115200;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "Valid init must return 0");
    zassert_equal(p_conf.transport, NMBS_TRANSPORT_RTU, "Transport must be RTU");
    zassert_equal(p_conf.read, nmbs_z_transport_serial_read, "Read callback bound");
    zassert_equal(p_conf.write, nmbs_z_transport_serial_write, "Write callback bound");
    zassert_equal(p_conf.flush, nmbs_z_transport_serial_flush, "Flush callback bound");
    zassert_equal(p_conf.arg, &transport, "Transport argument bound");
    zassert_equal(transport.t1_5_us, 750, "t1.5 timing calculated on init");
    zassert_equal(transport.t3_5_us, 1750, "t3.5 timing calculated on init");
}

/* RS-485 DE Pin assertion & drain timing */
ZTEST(nanomodbus_serial, test_rs485_de_pin_drain_timing)
{
    /* Review Focus 2: Ensure DE pin remains asserted until shift register drain */
    reset_mock_tracker();

    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_uart_dev),
        .de_gpio = {
            .port = DEVICE_GET(mock_gpio_dev),
            .pin = 2,
            .dt_flags = GPIO_ACTIVE_HIGH,
        },
        .baud_rate = 115200,
        .de_pre_delay_us = 20,
        .de_post_delay_us = 20,
    };
    nmbs_platform_conf p_conf;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "transport init must succeed");

    uint8_t payload[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};

    int32_t written = p_conf.write(payload, sizeof(payload), 1000, &transport);
    zassert_equal(written, sizeof(payload), "All bytes written");
    zassert_equal(mock_de_tracker.bytes_written, sizeof(payload), "All bytes reached UART");
    zassert_equal(mock_de_tracker.de_assert_count, 1, "DE asserted once");
    zassert_equal(mock_de_tracker.de_deassert_count, 1, "DE deasserted once");
    zassert_equal(mock_de_tracker.de_state, 0, "DE deasserted after write completes");
    zassert_false(mock_de_tracker.premature_deassert_detected,
                  "DE pin must never deassert prematurely before shift register drain");
}

/* RS-485 write without DE GPIO */
ZTEST(nanomodbus_serial, test_rs485_write_without_de_gpio)
{
    reset_mock_tracker();

    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_uart_dev),
        .de_gpio = { .port = NULL },
        .baud_rate = 115200,
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };
    nmbs_platform_conf p_conf;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "transport init without DE GPIO must succeed");

    uint8_t payload[] = {0x01, 0x06, 0x00, 0x01, 0x00, 0x03, 0x98, 0x0B};
    int32_t written = p_conf.write(payload, sizeof(payload), 1000, &transport);
    zassert_equal(written, sizeof(payload), "All bytes written");
    zassert_equal(mock_de_tracker.bytes_written, sizeof(payload), "All bytes reached UART");
    zassert_equal(mock_de_tracker.de_assert_count, 0, "DE never asserted when port is NULL");
}

/* Serial read and flush */
ZTEST(nanomodbus_serial, test_serial_read_and_flush)
{    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = NULL,
        .de_gpio = { .port = NULL },
        .baud_rate = 115200,
    };
    nmbs_platform_conf p_conf;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "init must succeed");

    /* Non-blocking read on empty buffer returns 0 */
    uint8_t rx_buf[16] = {0};
    int32_t n = p_conf.read(rx_buf, sizeof(rx_buf), 0, &transport);
    zassert_equal(n, 0, "Non-blocking read on empty ring buffer returns 0");

    /* Put bytes into ring buffer */
    uint8_t sample_data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    ring_buf_put(&transport.rx_rb, sample_data, sizeof(sample_data));

    /* Read back 4 bytes */
    n = p_conf.read(rx_buf, 4, 10, &transport);
    zassert_equal(n, 4, "Read 4 bytes successfully");
    zassert_mem_equal(rx_buf, sample_data, 4, "Read bytes match sample data");

    /* Test flush */
    ring_buf_put(&transport.rx_rb, sample_data, sizeof(sample_data));
    p_conf.flush(NULL, &transport);
    n = p_conf.read(rx_buf, sizeof(rx_buf), 0, &transport);
    zassert_equal(n, 0, "Read after flush returns 0");
}

/* Transport receive path used by IRQ/ASYNC callbacks */
ZTEST(nanomodbus_serial, test_serial_rx_put_and_overflow)
{
    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = NULL,
        .de_mode = NMBS_Z_SERIAL_DE_NONE,
        .rx_mode = NMBS_Z_SERIAL_RX_EXTERNAL,
        .baud_rate = 115200,
    };
    nmbs_platform_conf p_conf;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "init must succeed");

    uint8_t payload[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    zassert_equal(nmbs_z_serial_transport_rx_put(&transport, payload, sizeof(payload)),
                  sizeof(payload), "rx_put stores all bytes");

    uint8_t rx[sizeof(payload)] = {0};
    zassert_equal(p_conf.read(rx, sizeof(rx), 10, &transport), sizeof(rx), "read returns queued bytes");
    zassert_mem_equal(rx, payload, sizeof(payload), "queued bytes match");

    /* A frame that does not fit must be reported as a transport overflow. */
    uint8_t big[CONFIG_NANOMODBUS_SERIAL_RX_BUF_SIZE + 1];

    memset(big, 0xA5, sizeof(big));
    zassert_equal(nmbs_z_serial_transport_rx_put(&transport, big, sizeof(big)),
                  CONFIG_NANOMODBUS_SERIAL_RX_BUF_SIZE, "ring accepts only its capacity");
    zassert_equal(p_conf.read(rx, sizeof(rx), 0, &transport), -EOVERFLOW,
                  "overflow is surfaced to the protocol layer");

    /* After the overflow error the transport is usable again. */
    zassert_equal(nmbs_z_serial_transport_rx_put(&transport, payload, sizeof(payload)),
                  sizeof(payload), "transport recovers after overflow");
    zassert_equal(p_conf.read(rx, sizeof(rx), 10, &transport), sizeof(rx), "read works after overflow");
}

/* Hardware DE without runtime UART configuration must not touch a GPIO */
ZTEST(nanomodbus_serial, test_serial_explicit_hardware_de_skips_gpio)
{
    reset_mock_tracker();

    nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_uart_dev),
        .de_gpio = {
            .port = DEVICE_GET(mock_gpio_dev),
            .pin = 2,
            .dt_flags = GPIO_ACTIVE_HIGH,
        },
        .baud_rate = 115200,
        .de_mode = NMBS_Z_SERIAL_DE_HARDWARE,
    };
    nmbs_platform_conf p_conf;
    int ret = nmbs_z_serial_transport_init(&transport, &cfg, &p_conf);
    zassert_equal(ret, 0, "explicit hardware DE must be accepted when the driver cannot report config");
    zassert_true(transport.de_hw, "hardware DE flag set");
    zassert_false(transport.de_gpio, "GPIO DE flag cleared");

    uint8_t payload[] = {0x01, 0x06, 0x00, 0x01, 0x00, 0x03, 0x98, 0x0B};
    zassert_equal(p_conf.write(payload, sizeof(payload), 1000, &transport), sizeof(payload),
                  "write succeeds");
    zassert_equal(mock_de_tracker.de_assert_count, 0, "hardware DE must not drive the GPIO");
}

ZTEST_SUITE(nanomodbus_serial, NULL, NULL, NULL, NULL, NULL);
