/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>

static void test_shell_after(void *fixture)
{
	ARG_UNUSED(fixture);
	nmbs_z_shell_unregister_client("rtu0");
	nmbs_z_shell_unregister_client("dup");
	nmbs_z_shell_unregister_client("my_client");
	nmbs_z_shell_unregister_server("srv0");
	nmbs_z_shell_unregister_server("srv_test");
	nmbs_z_shell_unregister_server("srv_rtu_h");
	nmbs_z_shell_unregister_server("srv_tcp_h");
}

static void *test_shell_setup(void)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();

	WAIT_FOR(shell_ready(sh), 20000, k_msleep(1));
	zassert_true(shell_ready(sh), "timed out waiting for dummy shell backend");

	return NULL;
}

ZTEST_SUITE(nanomodbus_shell, NULL, test_shell_setup, NULL, test_shell_after, NULL);

ZTEST(nanomodbus_shell, test_shell_interface_and_kconfig_baseline)
{
    /* Verify Kconfig configuration bounds */
    zassert_equal(CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS, 4, "Default client limit mismatch");
    zassert_equal(CONFIG_NANOMODBUS_SHELL_MAX_SERVERS, 4, "Default server limit mismatch");
    zassert_equal(CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, 64, "Default data words limit mismatch");
    zassert_equal(NMBS_Z_SHELL_NAME_MAX_LEN, 16, "Name max len mismatch");

    /* Null pointer argument validation */
    zassert_equal(nmbs_z_shell_register_client(NULL, NULL), -EINVAL, "Expected -EINVAL on NULL client");
    zassert_equal(nmbs_z_shell_unregister_client(NULL), -EINVAL, "Expected -EINVAL on NULL name");
    zassert_equal(nmbs_z_shell_register_server(NULL, NULL, NULL), -EINVAL, "Expected -EINVAL on NULL server");
    zassert_equal(nmbs_z_shell_unregister_server(NULL), -EINVAL, "Expected -EINVAL on NULL name");
}

ZTEST(nanomodbus_shell, test_shell_register_client_success)
{
    static nmbs_z_client_t dummy_client;
    zassert_equal(nmbs_z_shell_register_client("rtu0", &dummy_client), 0, "Registration failed");
    zassert_equal(nmbs_z_shell_unregister_client("rtu0"), 0, "Unregistration failed");
}

ZTEST(nanomodbus_shell, test_shell_register_client_duplicate)
{
    static nmbs_z_client_t dummy_client1;
    static nmbs_z_client_t dummy_client2;
    zassert_equal(nmbs_z_shell_register_client("dup", &dummy_client1), 0, "First registration failed");
    zassert_equal(nmbs_z_shell_register_client("dup", &dummy_client2), -EEXIST, "Expected -EEXIST for duplicate");
    zassert_equal(nmbs_z_shell_unregister_client("dup"), 0, "Cleanup failed");
}

ZTEST(nanomodbus_shell, test_shell_register_client_boundary_exhaustion)
{
    static nmbs_z_client_t clients[CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS + 1];
    char name[16];

    /* Register up to capacity */
    for (int i = 0; i < CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS; i++) {
        snprintf(name, sizeof(name), "c%d", i);
        zassert_equal(nmbs_z_shell_register_client(name, &clients[i]), 0, "Registration failed at %d", i);
    }

    /* Next one must fail with -ENOMEM */
    zassert_equal(nmbs_z_shell_register_client("overflow", &clients[CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS]),
                  -ENOMEM, "Expected -ENOMEM on registry full");

    /* Cleanup */
    for (int i = 0; i < CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS; i++) {
        snprintf(name, sizeof(name), "c%d", i);
        zassert_equal(nmbs_z_shell_unregister_client(name), 0, "Cleanup failed at %d", i);
    }
}

ZTEST(nanomodbus_shell, test_shell_unregister_client_not_found)
{
    zassert_equal(nmbs_z_shell_unregister_client("nonexistent"), -ENOENT, "Expected -ENOENT");
}

ZTEST(nanomodbus_shell, test_shell_register_server_lifecycle)
{
    int dummy_ctx = 1;
    zassert_equal(nmbs_z_shell_register_server("srv0", &dummy_ctx, (nmbs_z_server_diag_cb)0x1), 0, "Reg failed");
    zassert_equal(nmbs_z_shell_register_server("srv0", &dummy_ctx, (nmbs_z_server_diag_cb)0x1), -EEXIST, "Exp -EEXIST");
    zassert_equal(nmbs_z_shell_unregister_server("srv0"), 0, "Unreg failed");
    zassert_equal(nmbs_z_shell_unregister_server("srv0"), -ENOENT, "Exp -ENOENT");
}

ZTEST(nanomodbus_shell, test_shell_parser_radix_valid)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    uint16_t val = 0;

    /* Decimal */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "1234", "test", &val), 0, "Decimal parse failed");
    zassert_equal(val, 1234, "Decimal value mismatch");

    /* Hexadecimal */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "0x04D2", "test", &val), 0, "Hex parse failed");
    zassert_equal(val, 1234, "Hex value mismatch");

    /* Hexadecimal boundary 0xFFFF */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "0xFFFF", "test", &val), 0, "Max hex parse failed");
    zassert_equal(val, 65535, "Max hex value mismatch");
}

ZTEST(nanomodbus_shell, test_shell_parser_malformed_radix)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    uint16_t val = 0;

    /* Invalid hex char */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "0xZZ", "addr", &val), -EINVAL, "Expected -EINVAL for 0xZZ");

    /* Empty string */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "", "addr", &val), -EINVAL, "Expected -EINVAL for empty");

    /* Trailing garbage */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "123abc", "addr", &val), -EINVAL, "Expected -EINVAL for trailing text");

    /* Negative number */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "-1", "addr", &val), -EINVAL, "Expected -EINVAL for negative number");

    /* Numerical overflow (> 65535) */
    zassert_equal(nmbs_z_shell_parse_u16(sh, "70000", "addr", &val), -ERANGE, "Expected -ERANGE on overflow");
}

ZTEST(nanomodbus_shell, test_shell_parser_quantity_boundary_overflow)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    uint16_t qty = 0;

    /* Zero quantity */
    zassert_equal(nmbs_z_shell_parse_quantity(sh, "0", CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty),
                  -EINVAL, "Zero quantity should fail");

    /* Valid quantity */
    zassert_equal(nmbs_z_shell_parse_quantity(sh, "64", CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty),
                  0, "Max quantity failed");
    zassert_equal(qty, 64, "Quantity mismatch");

    /* Out of bounds (> MAX_DATA_WORDS) */
    zassert_equal(nmbs_z_shell_parse_quantity(sh, "65", CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty),
                  -EINVAL, "Quantity > MAX_DATA_WORDS should fail");
}

ZTEST(nanomodbus_shell, test_shell_parser_boolean_variants)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    bool val = false;

    /* Truthy variants */
    const char *truthy[] = {"1", "on", "ON", "true", "TRUE", "high", "HIGH"};
    for (int i = 0; i < ARRAY_SIZE(truthy); i++) {
        zassert_equal(nmbs_z_shell_parse_bool(sh, truthy[i], "coil", &val), 0, "Truthy %s failed", truthy[i]);
        zassert_true(val, "Truthy %s not true", truthy[i]);
    }

    /* Falsy variants */
    const char *falsy[] = {"0", "off", "OFF", "false", "FALSE", "low", "LOW"};
    for (int i = 0; i < ARRAY_SIZE(falsy); i++) {
        zassert_equal(nmbs_z_shell_parse_bool(sh, falsy[i], "coil", &val), 0, "Falsy %s failed", falsy[i]);
        zassert_false(val, "Falsy %s not false", falsy[i]);
    }

    /* Invalid boolean */
    zassert_equal(nmbs_z_shell_parse_bool(sh, "maybe", "coil", &val), -EINVAL, "Invalid boolean accepted");
}

ZTEST(nanomodbus_shell, test_shell_parser_options)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    struct nmbs_z_cli_options opts;

    char *argv[] = {"cmd", "-c", "my_client", "-u", "5", "-t", "250", "100", "20"};
    int ret = nmbs_z_shell_parse_options(sh, ARRAY_SIZE(argv), argv, &opts);
    zassert_equal(ret, 0, "Failed to parse options");
    zassert_str_equal(opts.client_name, "my_client", "Client name mismatch");
    zassert_true(opts.unit_id_specified, "unit_id_specified should be true");
    zassert_equal(opts.unit_id, 5, "Unit ID mismatch");
    zassert_equal(opts.timeout_ms, 250, "Timeout mismatch");
    zassert_equal(opts.positional_count, 2, "Positional count mismatch");
    zassert_str_equal(argv[opts.positional_idx[0]], "100", "Arg 0 mismatch");
    zassert_str_equal(argv[opts.positional_idx[1]], "20", "Arg 1 mismatch");

    char *bad_argv[] = {"cmd", "-unknown"};
    zassert_equal(nmbs_z_shell_parse_options(sh, ARRAY_SIZE(bad_argv), bad_argv, &opts), -EINVAL, "Unknown option should fail");
}

static uint8_t s_mock_force_exception = 0;
static uint16_t s_mock_holding[16] = {0x03E8, 0x07D0, 0x0000, 0xFFFF};
static uint8_t s_mock_coils = 0x4D; /* 0b01001101 */
static uint8_t s_mock_rx_buf[256];
static uint16_t s_mock_rx_len;
static uint16_t s_mock_rx_pos;
static nmbs_z_client_t s_test_client;
static nmbs_platform_conf s_test_platform;

static void mock_append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = nmbs_crc_calc(buf, len, NULL);
    buf[len] = (uint8_t)((crc >> 8) & 0xFF);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
}

static int32_t shell_mock_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);
    ARG_UNUSED(arg);

    if (s_mock_rx_pos + count > s_mock_rx_len) {
        return 0;
    }
    memcpy(buf, &s_mock_rx_buf[s_mock_rx_pos], count);
    s_mock_rx_pos += count;
    return count;
}

static int32_t shell_mock_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);
    ARG_UNUSED(arg);

    if (count < 4) {
        return count;
    }

    uint8_t unit_id = buf[0];
    uint8_t fc = buf[1];
    s_mock_rx_pos = 0;
    s_mock_rx_len = 0;

    if (s_mock_force_exception != 0) {
        s_mock_rx_buf[0] = unit_id;
        s_mock_rx_buf[1] = fc | 0x80;
        s_mock_rx_buf[2] = s_mock_force_exception;
        mock_append_crc(s_mock_rx_buf, 3);
        s_mock_rx_len = 5;
        return count;
    }

    if (fc == 0x01 || fc == 0x02) {
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        ARG_UNUSED(addr);
        uint8_t bytes = (uint8_t)((qty + 7) / 8);
        s_mock_rx_buf[0] = unit_id;
        s_mock_rx_buf[1] = fc;
        s_mock_rx_buf[2] = bytes;
        for (uint8_t b = 0; b < bytes; b++) {
            s_mock_rx_buf[3 + b] = s_mock_coils;
        }
        mock_append_crc(s_mock_rx_buf, 3 + bytes);
        s_mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x03 || fc == 0x04) {
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint8_t bytes = (uint8_t)(qty * 2);
        s_mock_rx_buf[0] = unit_id;
        s_mock_rx_buf[1] = fc;
        s_mock_rx_buf[2] = bytes;
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t val = 0;
            if (addr + i < ARRAY_SIZE(s_mock_holding)) {
                val = s_mock_holding[addr + i];
            }
            s_mock_rx_buf[3 + 2 * i] = (uint8_t)((val >> 8) & 0xFF);
            s_mock_rx_buf[4 + 2 * i] = (uint8_t)(val & 0xFF);
        }
        mock_append_crc(s_mock_rx_buf, 3 + bytes);
        s_mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x05 || fc == 0x06) {
        memcpy(s_mock_rx_buf, buf, count);
        s_mock_rx_len = count;
    } else if (fc == 0x0F || fc == 0x10) {
        s_mock_rx_buf[0] = unit_id;
        s_mock_rx_buf[1] = fc;
        s_mock_rx_buf[2] = buf[2];
        s_mock_rx_buf[3] = buf[3];
        s_mock_rx_buf[4] = buf[4];
        s_mock_rx_buf[5] = buf[5];
        mock_append_crc(s_mock_rx_buf, 6);
        s_mock_rx_len = 8;
    }
    return count;
}

static void ensure_test_client_registered(void)
{
    static bool initialized;
    if (!initialized) {
        nmbs_platform_conf_create(&s_test_platform);
        s_test_platform.transport = NMBS_TRANSPORT_RTU;
        s_test_platform.read = shell_mock_read;
        s_test_platform.write = shell_mock_write;
        int ret = nmbs_z_client_init(&s_test_client, &s_test_platform);
        zassert_equal(ret, 0, "Client init failed");
        initialized = true;
    }
    nmbs_z_shell_unregister_client("rtu0");
    int ret = nmbs_z_shell_register_client("rtu0", &s_test_client);
    zassert_equal(ret, 0, "Registration of rtu0 failed");
}

static K_SEM_DEFINE(s_sem_shell_lock_held, 0, 1);
static K_SEM_DEFINE(s_sem_shell_release_lock, 0, 1);
static K_THREAD_STACK_DEFINE(s_shell_holder_stack, 2048);
static struct k_thread s_shell_holder_thread;

static void shell_lock_holder_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    nmbs_z_client_t *client = (nmbs_z_client_t *)p1;

    int ret = k_mutex_lock(&client->lock, K_FOREVER);
    zassert_equal(ret, 0, "Holder thread failed to acquire lock");
    k_sem_give(&s_sem_shell_lock_held);

    k_sem_take(&s_sem_shell_release_lock, K_FOREVER);
    k_mutex_unlock(&client->lock);
}

ZTEST(nanomodbus_shell, test_shell_cmd_read_holding_fc03)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-holding 0 4");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "1000 (0x03E8)"), "Missing reg 0 in output: %s", out);
    zassert_not_null(strstr(out, "2000 (0x07D0)"), "Missing reg 1 in output: %s", out);
    zassert_not_null(strstr(out, "65535 (0xFFFF)"), "Missing reg 3 in output: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_read_input_fc04)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-input 0 4");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "1000 (0x03E8)"), "Missing reg 0 in output: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_read_coils_fc01)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-coils 0 8");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "(0x4D)"), "Missing coil bitfield byte: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_read_discrete_fc02)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-discrete 0 8");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "(0x4D)"), "Missing discrete bitfield byte: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_write_reg_fc06)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs write-register 0x0010 1234");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "OK: Wrote register at address 0x0010"), "Missing OK message: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_write_coil_fc05)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs write-coil 0x0020 on");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "OK: Wrote coil at address 0x0020"), "Missing OK message: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_write_coils_fc15)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs write-coils 0 1 0 1 1");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "OK: Wrote 4 coil(s) starting at address 0x0000"), "Missing OK message: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_write_registers_fc16)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs write-registers 0x0100 10 20 30");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "OK: Wrote 3 register(s) starting at address 0x0100"), "Missing OK message: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_modbus_exception_decoding)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    /* Configure mock transport to respond with Modbus Exception 0x02 */
    s_mock_force_exception = 0x02;

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-holding 0x5000 1");
    zassert_not_equal(ret, 0, "Expected non-zero return code on exception");

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Modbus Exception:"), "Missing exception header: %s", out);
    zassert_not_null(strstr(out, "(Code 0x02)"), "Missing exception code 0x02: %s", out);
    s_mock_force_exception = 0;
}

ZTEST(nanomodbus_shell, test_shell_cmd_lock_contention_timeout)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    k_sem_reset(&s_sem_shell_lock_held);
    k_sem_reset(&s_sem_shell_release_lock);

    k_tid_t tid = k_thread_create(&s_shell_holder_thread,
                    s_shell_holder_stack,
                    K_THREAD_STACK_SIZEOF(s_shell_holder_stack),
                    shell_lock_holder_thread,
                    &s_test_client, NULL, NULL,
                    K_PRIO_PREEMPT(1), 0, K_NO_WAIT);

    k_sem_take(&s_sem_shell_lock_held, K_FOREVER);

    /* Run command with a short 50 ms timeout */
    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs read-holding -t 50 0 1");
    zassert_not_equal(ret, 0, "Expected failure on lock contention");

    k_sem_give(&s_sem_shell_release_lock);
    k_thread_join(tid, K_FOREVER);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Client busy: Lock timeout expired"), "Missing timeout msg: %s", out);
}

static struct nmbs_z_server_stats s_mock_server_stats;

static void mock_server_init_stats(void)
{
    s_mock_server_stats.rx_frames = 100;
    s_mock_server_stats.tx_frames = 98;
    s_mock_server_stats.rx_errors = 2;
    s_mock_server_stats.exceptions_sent = 1;
    s_mock_server_stats.requests_handled = 97;
}

static int mock_server_diag_cb(void *ctx, struct nmbs_z_server_info *info, bool reset)
{
    if (info) {
        info->state = NMBS_Z_SERVER_STATE_RUNNING;
        info->unit_id = 1;
        info->port = 0;
        info->stats = s_mock_server_stats;
    }

    if (reset) {
        memset(&s_mock_server_stats, 0, sizeof(s_mock_server_stats));
    }
    return 0;
}

ZTEST(nanomodbus_shell, test_shell_cmd_list)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    ensure_test_client_registered();

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs list");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "nanoMODBUS Registered Instances:"), "Missing header: %s", out);
    zassert_not_null(strstr(out, "Clients:"), "Missing clients section: %s", out);
    zassert_not_null(strstr(out, "Servers:"), "Missing servers section: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_server_status)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    mock_server_init_stats();
    nmbs_z_shell_register_server("srv_test", (void *)0x1234, mock_server_diag_cb);

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs status -s srv_test");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Frames Received:    100"), "Missing rx_frames: %s", out);
    zassert_not_null(strstr(out, "Checksum Errors:    2"), "Missing rx_errors: %s", out);
    zassert_not_null(strstr(out, "Operational State:  RUNNING"), "Missing state: %s", out);
}

ZTEST(nanomodbus_shell, test_shell_cmd_server_reset_stats)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;

    mock_server_init_stats();
    nmbs_z_shell_register_server("srv_test", (void *)0x1234, mock_server_diag_cb);

    shell_backend_dummy_clear_output(sh);
    int ret = shell_execute_cmd(sh, "nmbs reset-stats -s srv_test");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "reset to 0"), "Missing reset confirmation: %s", out);

    /* Verify subsequent status displays 0 */
    shell_backend_dummy_clear_output(sh);
    shell_execute_cmd(sh, "nmbs status -s srv_test");
    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Frames Received:    0"), "Stats not reset: %s", out);

    nmbs_z_shell_unregister_server("srv_test");
}

ZTEST(nanomodbus_shell, test_shell_cmd_server_rtu_helper)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;
    static nmbs_z_server_rtu_t rtu_srv;

    memset(&rtu_srv, 0, sizeof(rtu_srv));
    rtu_srv.server_address = 7;
    atomic_set(&rtu_srv.running, 1);

    int ret = nmbs_z_shell_register_server_rtu("srv_rtu_h", &rtu_srv);
    zassert_equal(ret, 0, "Registration failed: %d", ret);

    shell_backend_dummy_clear_output(sh);
    ret = shell_execute_cmd(sh, "nmbs status -s srv_rtu_h");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Modbus Server 'srv_rtu_h' Status:"), "Missing status header: %s", out);
    zassert_not_null(strstr(out, "Operational State:  RUNNING"), "Missing state: %s", out);
    zassert_not_null(strstr(out, "Modbus Unit ID:     7"), "Missing unit ID: %s", out);

    /* Test reset stats */
    shell_backend_dummy_clear_output(sh);
    ret = shell_execute_cmd(sh, "nmbs reset-stats -s srv_rtu_h");
    zassert_equal(ret, 0, "Reset stats failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "reset to 0"), "Missing reset confirmation: %s", out);

    nmbs_z_shell_unregister_server("srv_rtu_h");
}

ZTEST(nanomodbus_shell, test_shell_cmd_server_tcp_helper)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    size_t out_len;
    const char *out;
    static nmbs_z_server_tcp_t tcp_srv;

    memset(&tcp_srv, 0, sizeof(tcp_srv));
    tcp_srv.port = 5020;
    atomic_set(&tcp_srv.running, 0);

    int ret = nmbs_z_shell_register_server_tcp("srv_tcp_h", &tcp_srv);
    zassert_equal(ret, 0, "Registration failed: %d", ret);

    shell_backend_dummy_clear_output(sh);
    ret = shell_execute_cmd(sh, "nmbs status -s srv_tcp_h");
    zassert_equal(ret, 0, "Execution failed: %d", ret);

    out = shell_backend_dummy_get_output(sh, &out_len);
    zassert_not_null(strstr(out, "Modbus Server 'srv_tcp_h' Status:"), "Missing status header: %s", out);
    zassert_not_null(strstr(out, "Operational State:  STOPPED"), "Missing state: %s", out);
    zassert_not_null(strstr(out, "TCP Port:           5020"), "Missing port: %s", out);

    nmbs_z_shell_unregister_server("srv_tcp_h");
}
