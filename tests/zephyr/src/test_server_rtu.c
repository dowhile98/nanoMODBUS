/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

/* Mock UART transmitter capturing responses */
static uint8_t mock_server_tx_buf[256];
static uint16_t mock_server_tx_len = 0;

static void mock_server_uart_poll_out(const struct device *dev, unsigned char out_char)
{
    ARG_UNUSED(dev);
    if (mock_server_tx_len < sizeof(mock_server_tx_buf)) {
        mock_server_tx_buf[mock_server_tx_len++] = (uint8_t)out_char;
    }
}

static DEVICE_API(uart, mock_server_uart_api) = {
    .poll_out = mock_server_uart_poll_out,
};

static int mock_server_dev_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

DEVICE_DEFINE(mock_server_uart_dev, "mock_server_uart", mock_server_dev_init, NULL,
              NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mock_server_uart_api);

/* Callback execution tracker */
static struct {
    bool read_holding_called;
    bool write_single_called;
    bool write_multiple_called;
    uint16_t read_address;
    uint16_t read_quantity;
    uint16_t write_address;
    uint16_t write_single_val;
    uint16_t write_quantity;
    uint16_t written_regs[16];
    uint8_t unit_id;
    uint32_t call_count;
} server_cb_tracker;

static void reset_server_tracker(void)
{
    memset(&server_cb_tracker, 0, sizeof(server_cb_tracker));
    mock_server_tx_len = 0;
    memset(mock_server_tx_buf, 0, sizeof(mock_server_tx_buf));
}

static nmbs_error mock_read_holding_registers(uint16_t address, uint16_t quantity,
                                             uint16_t *registers_out, uint8_t unit_id,
                                             void *arg)
{
    ARG_UNUSED(arg);
    server_cb_tracker.read_holding_called = true;
    server_cb_tracker.read_address = address;
    server_cb_tracker.read_quantity = quantity;
    server_cb_tracker.unit_id = unit_id;
    server_cb_tracker.call_count++;

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = 100 + address + i;
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error mock_write_single_register(uint16_t address, uint16_t value,
                                            uint8_t unit_id, void *arg)
{
    ARG_UNUSED(arg);
    server_cb_tracker.write_single_called = true;
    server_cb_tracker.write_address = address;
    server_cb_tracker.write_single_val = value;
    server_cb_tracker.unit_id = unit_id;
    server_cb_tracker.call_count++;
    return NMBS_ERROR_NONE;
}

static nmbs_error mock_write_multiple_registers(uint16_t address, uint16_t quantity,
                                               const uint16_t *registers, uint8_t unit_id,
                                               void *arg)
{
    ARG_UNUSED(arg);
    server_cb_tracker.write_multiple_called = true;
    server_cb_tracker.write_address = address;
    server_cb_tracker.write_quantity = quantity;
    server_cb_tracker.unit_id = unit_id;
    server_cb_tracker.call_count++;

    for (uint16_t i = 0; i < quantity && i < 16; i++) {
        server_cb_tracker.written_regs[i] = registers[i];
    }
    return NMBS_ERROR_NONE;
}

static void helper_append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = nmbs_crc_calc(buf, len, NULL);
    buf[len] = (uint8_t)((crc >> 8) & 0xFF);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
}

/* ========================================================================= */
/* Test 1: Full RTU request reception, callbacks and response transmission   */
/* ========================================================================= */
ZTEST(nanomodbus_server_rtu, test_full_rtu_requests_reception_and_response)
{
    reset_server_tracker();

    static nmbs_z_server_rtu_t server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_server_uart_dev),
        .baud_rate = 115200,
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };

    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;
    cbs.write_single_register = mock_write_single_register;
    cbs.write_multiple_registers = mock_write_multiple_registers;

    int ret = nmbs_z_server_rtu_init(&server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "Server init must succeed");

    /* --- Subtest 1A: FC 03 Read Holding Registers (Addr 10, Qty 2) --- */
    reset_server_tracker();
    uint8_t req_fc03[8] = {1, 3, 0x00, 0x0A, 0x00, 0x02, 0, 0};
    helper_append_crc(req_fc03, 6);

    for (int i = 0; i < sizeof(req_fc03); i++) {
        nmbs_z_server_rtu_rx_byte(&server, req_fc03[i]);
    }

    /* Wait for silence timer (1750 us) and workqueue execution */
    k_sleep(K_MSEC(5));

    zassert_true(server_cb_tracker.read_holding_called, "Read holding callback must be called");
    zassert_equal(server_cb_tracker.read_address, 10, "Read address must be 10");
    zassert_equal(server_cb_tracker.read_quantity, 2, "Read quantity must be 2");
    zassert_equal(server_cb_tracker.unit_id, 1, "Unit ID must be 1");

    /* Response should be 9 bytes: [Unit(1), FC(3), ByteCount(4), Reg0(0x00,0x6E), Reg1(0x00,0x6F), CRC(2)] */
    zassert_equal(mock_server_tx_len, 9, "FC03 response length must be 9 bytes (got %d)", mock_server_tx_len);
    zassert_equal(mock_server_tx_buf[0], 1, "Unit ID mismatch");
    zassert_equal(mock_server_tx_buf[1], 3, "FC mismatch");
    zassert_equal(mock_server_tx_buf[2], 4, "Byte count mismatch");
    zassert_equal(mock_server_tx_buf[3], 0x00, "Reg0 high byte mismatch");
    zassert_equal(mock_server_tx_buf[4], 110, "Reg0 low byte mismatch (100 + 10)");
    zassert_equal(mock_server_tx_buf[5], 0x00, "Reg1 high byte mismatch");
    zassert_equal(mock_server_tx_buf[6], 111, "Reg1 low byte mismatch (100 + 11)");

    /* --- Subtest 1B: FC 06 Write Single Register (Addr 20, Val 0x1234) --- */
    reset_server_tracker();
    uint8_t req_fc06[8] = {1, 6, 0x00, 0x14, 0x12, 0x34, 0, 0};
    helper_append_crc(req_fc06, 6);

    for (int i = 0; i < sizeof(req_fc06); i++) {
        nmbs_z_server_rtu_rx_byte(&server, req_fc06[i]);
    }

    k_sleep(K_MSEC(5));

    zassert_true(server_cb_tracker.write_single_called, "Write single callback must be called");
    zassert_equal(server_cb_tracker.write_address, 20, "Write address must be 20");
    zassert_equal(server_cb_tracker.write_single_val, 0x1234, "Write value must be 0x1234");
    zassert_equal(mock_server_tx_len, 8, "FC06 echo response length must be 8 bytes");
    zassert_mem_equal(mock_server_tx_buf, req_fc06, 8, "FC06 response must echo request");

    /* --- Subtest 1C: FC 16 Write Multiple Registers (Addr 30, Qty 2, Vals [0x1111, 0x2222]) --- */
    reset_server_tracker();
    uint8_t req_fc16[13] = {
        1, 16, 0x00, 0x1E, 0x00, 0x02, 4,
        0x11, 0x11, 0x22, 0x22,
        0, 0
    };
    helper_append_crc(req_fc16, 11);

    for (int i = 0; i < sizeof(req_fc16); i++) {
        nmbs_z_server_rtu_rx_byte(&server, req_fc16[i]);
    }

    k_sleep(K_MSEC(5));

    zassert_true(server_cb_tracker.write_multiple_called, "Write multiple callback must be called");
    zassert_equal(server_cb_tracker.write_address, 30, "Write address must be 30");
    zassert_equal(server_cb_tracker.write_quantity, 2, "Write quantity must be 2");
    zassert_equal(server_cb_tracker.written_regs[0], 0x1111, "Reg0 mismatch");
    zassert_equal(server_cb_tracker.written_regs[1], 0x2222, "Reg1 mismatch");
    zassert_equal(mock_server_tx_len, 8, "FC16 response length must be 8 bytes");
}

/* ========================================================================= */
/* Test 2 (Review Focus 3): Fragmented character delivery silence reset      */
/* ========================================================================= */
ZTEST(nanomodbus_server_rtu, test_fragmented_character_silence_reset)
{
    reset_server_tracker();

    static nmbs_z_server_rtu_t server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_server_uart_dev),
        .baud_rate = 115200, /* t3.5 = 1750 us */
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };

    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_rtu_init(&server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "Server init must succeed");

    uint8_t req[8] = {1, 3, 0x00, 0x05, 0x00, 0x01, 0, 0};
    helper_append_crc(req, 6);

    /*
     * Review Focus 3: Deliver characters with inter-byte gap of 500 us (< 1750 us).
     * Verify that the silence timer resets on every byte, workqueue is NOT triggered,
     * and callback is NOT executed while streaming is in progress.
     */
    for (int i = 0; i < 7; i++) {
        nmbs_z_server_rtu_rx_byte(&server, req[i]);
        k_sleep(K_USEC(500));

        zassert_equal(server_cb_tracker.call_count, 0,
                      "Workqueue must NOT be triggered mid-frame while gap < t3.5 (byte %d)", i);
        zassert_equal(mock_server_tx_len, 0,
                      "No response must be generated while frame is incomplete");
    }

    /* Deliver the 8th (final) byte */
    nmbs_z_server_rtu_rx_byte(&server, req[7]);

    /* Immediately after the last byte, callback must still not have run */
    zassert_equal(server_cb_tracker.call_count, 0,
                  "Workqueue must not run immediately before silence duration elapses");

    /* Now allow genuine silence >= t3.5 (5 ms >> 1.75 ms) */
    k_sleep(K_MSEC(5));

    /* Silence timer must have expired, submitted work, and executed nmbs_server_poll */
    zassert_equal(server_cb_tracker.call_count, 1,
                  "Workqueue must fire and execute callback once full t3.5 silence occurs");
    zassert_true(server_cb_tracker.read_holding_called, "Read holding registers callback executed");
    zassert_equal(server_cb_tracker.read_address, 5, "Address must match");
    zassert_equal(mock_server_tx_len, 7, "Response length must be 7 bytes (1+1+1+2+2)");
}

/* ========================================================================= */
/* Test 3: Stop server cancellation and start resumption                     */
/* ========================================================================= */
ZTEST(nanomodbus_server_rtu, test_server_stop_and_start)
{
    reset_server_tracker();

    static nmbs_z_server_rtu_t server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_server_uart_dev),
        .baud_rate = 115200,
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };

    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_rtu_init(&server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "Server init must succeed");

    /* Re-initializing a live instance would leak the workqueue thread */
    zassert_equal(nmbs_z_server_rtu_init(&server, &cfg, &cbs, 1), -EALREADY,
                  "Second init on the same instance must be rejected");

    /* Stop the server */
    ret = nmbs_z_server_rtu_stop(&server);
    zassert_equal(ret, 0, "Server stop must return 0");
    zassert_false(atomic_get(&server.running), "Server running flag must be false");

    /* Feed bytes while stopped */
    uint8_t req[8] = {1, 3, 0x00, 0x01, 0x00, 0x01, 0, 0};
    helper_append_crc(req, 6);

    for (int i = 0; i < sizeof(req); i++) {
        nmbs_z_server_rtu_rx_byte(&server, req[i]);
    }

    k_sleep(K_MSEC(5));

    zassert_equal(server_cb_tracker.call_count, 0,
                  "Stopped server must not trigger callbacks or process frames");
    zassert_equal(mock_server_tx_len, 0, "No TX while stopped");

    /* Restart the server */
    ret = nmbs_z_server_rtu_start(&server);
    zassert_equal(ret, 0, "Server start must return 0");
    zassert_true(atomic_get(&server.running), "Server running flag must be true");

    /* Now feed request and verify it is processed */
    for (int i = 0; i < sizeof(req); i++) {
        nmbs_z_server_rtu_rx_byte(&server, req[i]);
    }

    k_sleep(K_MSEC(5));

    zassert_equal(server_cb_tracker.call_count, 1,
                  "Started server must resume frame processing");
}

/* Parameter validation tests */
ZTEST(nanomodbus_server_rtu, test_server_init_parameter_validation)
{
    static nmbs_z_server_rtu_t server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = NULL,
        .baud_rate = 115200,
    };
    nmbs_callbacks cbs = {0};

    /* NULL pointers */
    zassert_equal(nmbs_z_server_rtu_init(NULL, &cfg, &cbs, 1), -EINVAL, "NULL server");
    zassert_equal(nmbs_z_server_rtu_init(&server, NULL, &cbs, 1), -EINVAL, "NULL config");
    zassert_equal(nmbs_z_server_rtu_init(&server, &cfg, NULL, 1), -EINVAL, "NULL callbacks");

    /* Invalid slave addresses */
    zassert_equal(nmbs_z_server_rtu_init(&server, &cfg, &cbs, 0), -EINVAL, "Address 0 invalid for RTU slave");
    zassert_equal(nmbs_z_server_rtu_init(&server, &cfg, &cbs, 248), -EINVAL, "Address > 247 invalid for RTU slave");

    /* Start / Stop NULL guards */
    zassert_equal(nmbs_z_server_rtu_start(NULL), -EINVAL, "NULL start");
    zassert_equal(nmbs_z_server_rtu_stop(NULL), -EINVAL, "NULL stop");

    /* Operating on a never-initialized instance must be rejected */
    zassert_equal(nmbs_z_server_rtu_start(&server), -EINVAL, "start before init");
}

ZTEST(nanomodbus_server_rtu, test_server_rtu_diagnostics_stats)
{
    static nmbs_z_server_rtu_t stats_server;
    const struct device *uart = DEVICE_GET(mock_server_uart_dev);

    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = uart,
        .baud_rate = 115200,
        .rx_mode = NMBS_Z_SERIAL_RX_EXTERNAL,
    };

    nmbs_callbacks cbs = {0};
    nmbs_callbacks_create(&cbs);
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_rtu_init(&stats_server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "Server init failed: %d", ret);

    struct nmbs_z_stats stats;
    ret = nmbs_z_server_rtu_get_stats(&stats_server, &stats);
    zassert_equal(ret, 0, "get_stats must succeed");
    zassert_equal(stats.rx_frames, 0, "rx_frames must be 0 initially");
    zassert_equal(stats.tx_frames, 0, "tx_frames must be 0 initially");
    zassert_equal(stats.crc_errors, 0, "crc_errors must be 0 initially");
    zassert_equal(stats.exceptions, 0, "exceptions must be 0 initially");
    zassert_equal(stats.timeouts, 0, "timeouts must be 0 initially");

    /* Send 1 valid FC03 request */
    reset_server_tracker();
    uint8_t req_fc03[8] = {1, 3, 0x00, 0x0A, 0x00, 0x02, 0, 0};
    helper_append_crc(req_fc03, 6);
    for (size_t i = 0; i < sizeof(req_fc03); i++) {
        nmbs_z_server_rtu_rx_byte(&stats_server, req_fc03[i]);
    }
    k_sleep(K_MSEC(10));

    ret = nmbs_z_server_rtu_get_stats(&stats_server, &stats);
    zassert_equal(ret, 0, "get_stats must succeed");
    zassert_equal(stats.rx_frames, 1, "rx_frames must be 1 (got %u)", stats.rx_frames);
    zassert_equal(stats.tx_frames, 1, "tx_frames must be 1 (got %u)", stats.tx_frames);
    zassert_equal(stats.crc_errors, 0, "crc_errors must be 0");
    zassert_equal(stats.exceptions, 0, "exceptions must be 0");

    /* Send 1 request with invalid CRC */
    reset_server_tracker();
    uint8_t req_bad_crc[8] = {1, 3, 0x00, 0x0A, 0x00, 0x02, 0xDE, 0xAD};
    for (size_t i = 0; i < sizeof(req_bad_crc); i++) {
        nmbs_z_server_rtu_rx_byte(&stats_server, req_bad_crc[i]);
    }
    k_sleep(K_MSEC(10));

    ret = nmbs_z_server_rtu_get_stats(&stats_server, &stats);
    zassert_equal(ret, 0, "get_stats must succeed");
    zassert_equal(stats.crc_errors, 1, "crc_errors must be 1 (got %u)", stats.crc_errors);

    /* Send 1 request with unsupported function code (causes Modbus exception 1) */
    reset_server_tracker();
    uint8_t req_unsupported_fc[8] = {1, 0x18, 0x00, 0x00, 0x00, 0x00, 0, 0};
    helper_append_crc(req_unsupported_fc, 6);
    for (size_t i = 0; i < sizeof(req_unsupported_fc); i++) {
        nmbs_z_server_rtu_rx_byte(&stats_server, req_unsupported_fc[i]);
    }
    k_sleep(K_MSEC(10));

    ret = nmbs_z_server_rtu_get_stats(&stats_server, &stats);
    zassert_equal(ret, 0, "get_stats must succeed");
    zassert_equal(stats.exceptions, 1, "exceptions must be 1 (got %u)", stats.exceptions);
    zassert_equal(stats.rx_frames, 2, "rx_frames must be 2 (got %u)", stats.rx_frames);
    zassert_equal(stats.tx_frames, 2, "tx_frames must be 2 (got %u)", stats.tx_frames);

    /* Test reset_stats */
    ret = nmbs_z_server_rtu_reset_stats(&stats_server);
    zassert_equal(ret, 0, "reset_stats must succeed");
    ret = nmbs_z_server_rtu_get_stats(&stats_server, &stats);
    zassert_equal(ret, 0, "get_stats must succeed");
    zassert_equal(stats.rx_frames, 0, "rx_frames must be 0 after reset");
    zassert_equal(stats.tx_frames, 0, "tx_frames must be 0 after reset");
    zassert_equal(stats.crc_errors, 0, "crc_errors must be 0 after reset");
    zassert_equal(stats.exceptions, 0, "exceptions must be 0 after reset");
    zassert_equal(stats.timeouts, 0, "timeouts must be 0 after reset");

    /* Parameter validation */
    zassert_equal(nmbs_z_server_rtu_get_stats(NULL, &stats), -EINVAL, "NULL server get");
    zassert_equal(nmbs_z_server_rtu_get_stats(&stats_server, NULL), -EINVAL, "NULL stats get");
    zassert_equal(nmbs_z_server_rtu_reset_stats(NULL), -EINVAL, "NULL server reset");

    nmbs_z_server_rtu_stop(&stats_server);
}

/* ========================================================================= */
/* Test 5: RTU Server set_address and get_address                            */
/* ========================================================================= */
ZTEST(nanomodbus_server_rtu, test_server_rtu_set_and_get_address)
{
    static nmbs_z_server_rtu_t addr_server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_server_uart_dev),
        .baud_rate = 115200,
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };
    nmbs_callbacks cbs = {0};

    int ret = nmbs_z_server_rtu_init(&addr_server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "Server init must succeed");

    /* Verify initial address */
    zassert_equal(nmbs_z_server_rtu_get_address(&addr_server), 1, "Initial address must be 1");

    /* Set valid address */
    ret = nmbs_z_server_rtu_set_address(&addr_server, 42);
    zassert_equal(ret, 0, "set_address must succeed");
    zassert_equal(nmbs_z_server_rtu_get_address(&addr_server), 42, "get_address must return 42");
    zassert_equal(addr_server.core.address_rtu, 42, "core address_rtu must be 42");
    zassert_equal(addr_server.server_address, 42, "server_address must be 42");

    /* Parameter validation */
    zassert_equal(nmbs_z_server_rtu_set_address(NULL, 10), -EINVAL, "NULL server set_address");
    zassert_equal(nmbs_z_server_rtu_set_address(&addr_server, 0), -EINVAL, "slave_id 0 is invalid");
    zassert_equal(nmbs_z_server_rtu_set_address(&addr_server, 248), -EINVAL, "slave_id 248 is invalid");
    zassert_equal(nmbs_z_server_rtu_get_address(NULL), 0, "NULL server get_address returns 0");

    nmbs_z_server_rtu_stop(&addr_server);
}

/* ========================================================================= */
/* Test 6: RTU Server reconfiguration and selective address response         */
/* ========================================================================= */
ZTEST(nanomodbus_server_rtu, test_server_rtu_reconfigure)
{
    reset_server_tracker();

    static nmbs_z_server_rtu_t reconfig_server;
    nmbs_z_serial_transport_config_t cfg = {
        .uart_dev = DEVICE_GET(mock_server_uart_dev),
        .baud_rate = 115200,
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };

    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_rtu_init(&reconfig_server, &cfg, &cbs, 1);
    zassert_equal(ret, 0, "Server init must succeed");

    /* Use short timeout so ignored requests destined for other slaves don't block tests */
    nmbs_set_read_timeout(&reconfig_server.core, 2);
    nmbs_set_byte_timeout(&reconfig_server.core, 2);

    /* Parameter validation */
    zassert_equal(nmbs_z_server_rtu_reconfigure(NULL, NULL, 1), -EINVAL, "NULL server");
    zassert_equal(nmbs_z_server_rtu_reconfigure(&reconfig_server, NULL, 248), -EINVAL, "slave_id > 247");
    struct uart_config invalid_cfg = { .baudrate = 0 };
    zassert_equal(nmbs_z_server_rtu_reconfigure(&reconfig_server, &invalid_cfg, 1), -EINVAL, "0 baudrate");

    /* Initial state: responds to address 1 */
    reset_server_tracker();
    uint8_t req_fc03_addr1[8] = {1, 3, 0x00, 0x0A, 0x00, 0x02, 0, 0};
    helper_append_crc(req_fc03_addr1, 6);

    for (size_t i = 0; i < sizeof(req_fc03_addr1); i++) {
        nmbs_z_server_rtu_rx_byte(&reconfig_server, req_fc03_addr1[i]);
    }
    k_sleep(K_MSEC(10));

    zassert_true(server_cb_tracker.read_holding_called, "Server must respond to address 1 initially");
    zassert_equal(server_cb_tracker.unit_id, 1, "Unit ID must be 1");
    zassert_equal(mock_server_tx_len, 9, "Response length must be 9 bytes");

    /* Reconfigure with uart_cfg == NULL: only changes slave ID to 5 */
    ret = nmbs_z_server_rtu_reconfigure(&reconfig_server, NULL, 5);
    zassert_equal(ret, 0, "Reconfigure with NULL uart_cfg must succeed");
    zassert_equal(nmbs_z_server_rtu_get_address(&reconfig_server), 5, "Slave address must be 5");
    zassert_equal(reconfig_server.transport.cfg.baud_rate, 115200, "Baud rate must remain 115200");

    /* Verify server ignores old address 1 */
    reset_server_tracker();
    for (size_t i = 0; i < sizeof(req_fc03_addr1); i++) {
        nmbs_z_server_rtu_rx_byte(&reconfig_server, req_fc03_addr1[i]);
    }
    k_sleep(K_MSEC(10));

    zassert_false(server_cb_tracker.read_holding_called, "Server must NOT respond to old address 1");
    zassert_equal(mock_server_tx_len, 0, "No TX for ignored address 1");

    /* Verify server responds to new address 5 */
    reset_server_tracker();
    uint8_t req_fc03_addr5[8] = {5, 3, 0x00, 0x0A, 0x00, 0x02, 0, 0};
    helper_append_crc(req_fc03_addr5, 6);

    for (size_t i = 0; i < sizeof(req_fc03_addr5); i++) {
        nmbs_z_server_rtu_rx_byte(&reconfig_server, req_fc03_addr5[i]);
    }
    k_sleep(K_MSEC(10));

    zassert_true(server_cb_tracker.read_holding_called, "Server must respond to new address 5");
    zassert_equal(server_cb_tracker.unit_id, 5, "Unit ID must be 5");
    zassert_equal(mock_server_tx_len, 9, "Response length must be 9 bytes");

    /* Reconfigure with slave_id == 0: only changes UART settings */
    struct uart_config new_uart_cfg = {
        .baudrate = 57600,
        .parity = UART_CFG_PARITY_EVEN,
        .stop_bits = UART_CFG_STOP_BITS_2,
        .data_bits = UART_CFG_DATA_BITS_8,
    };
    ret = nmbs_z_server_rtu_reconfigure(&reconfig_server, &new_uart_cfg, 0);
    zassert_equal(ret, 0, "Reconfigure with slave_id 0 must succeed");
    zassert_equal(nmbs_z_server_rtu_get_address(&reconfig_server), 5, "Slave address must remain 5");
    zassert_equal(reconfig_server.transport.cfg.baud_rate, 57600, "Baud rate updated to 57600");
    zassert_equal(reconfig_server.transport.cfg.parity, UART_CFG_PARITY_EVEN, "Parity updated to EVEN");
    zassert_equal(reconfig_server.transport.cfg.stop_bits, UART_CFG_STOP_BITS_2, "Stop bits updated to 2");
    zassert_equal(reconfig_server.transport.cfg.data_bits, UART_CFG_DATA_BITS_8, "Data bits updated to 8");

    /* Verify server continues responding to address 5 after UART reconfiguration */
    reset_server_tracker();
    for (size_t i = 0; i < sizeof(req_fc03_addr5); i++) {
        nmbs_z_server_rtu_rx_byte(&reconfig_server, req_fc03_addr5[i]);
    }
    k_sleep(K_MSEC(10));

    zassert_true(server_cb_tracker.read_holding_called, "Server must still respond to address 5");
    zassert_equal(server_cb_tracker.unit_id, 5, "Unit ID must be 5");

    /* Reconfigure both UART settings and slave address simultaneously */
    new_uart_cfg.baudrate = 19200;
    ret = nmbs_z_server_rtu_reconfigure(&reconfig_server, &new_uart_cfg, 7);
    zassert_equal(ret, 0, "Simultaneous reconfigure must succeed");
    zassert_equal(nmbs_z_server_rtu_get_address(&reconfig_server), 7, "Slave address must be 7");
    zassert_equal(reconfig_server.transport.cfg.baud_rate, 19200, "Baud rate must be 19200");

    /* Verify responds only to 7 */
    reset_server_tracker();
    for (size_t i = 0; i < sizeof(req_fc03_addr5); i++) {
        nmbs_z_server_rtu_rx_byte(&reconfig_server, req_fc03_addr5[i]);
    }
    k_sleep(K_MSEC(10));
    zassert_false(server_cb_tracker.read_holding_called, "Server must NOT respond to address 5");

    reset_server_tracker();
    uint8_t req_fc03_addr7[8] = {7, 3, 0x00, 0x0A, 0x00, 0x02, 0, 0};
    helper_append_crc(req_fc03_addr7, 6);
    for (size_t i = 0; i < sizeof(req_fc03_addr7); i++) {
        nmbs_z_server_rtu_rx_byte(&reconfig_server, req_fc03_addr7[i]);
    }
    k_sleep(K_MSEC(10));
    zassert_true(server_cb_tracker.read_holding_called, "Server must respond to address 7");
    zassert_equal(server_cb_tracker.unit_id, 7, "Unit ID must be 7");

    nmbs_z_server_rtu_stop(&reconfig_server);
}

ZTEST_SUITE(nanomodbus_server_rtu, NULL, NULL, NULL, NULL, NULL);
