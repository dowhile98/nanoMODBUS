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
