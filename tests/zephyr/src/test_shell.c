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

