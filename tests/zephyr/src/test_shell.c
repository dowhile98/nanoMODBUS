/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/nanomodbus/nanomodbus_z_shell.h>

ZTEST_SUITE(nanomodbus_shell, NULL, NULL, NULL, NULL, NULL);

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


