/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_shell.h>
#include <errno.h>

#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT
int nmbs_z_shell_register_client(const char *name, nmbs_z_client_t *client)
{
	if (!name || !client) {
		return -EINVAL;
	}
	return -ENOSYS;
}

int nmbs_z_shell_unregister_client(const char *name)
{
	if (!name) {
		return -EINVAL;
	}
	return -ENOSYS;
}
#endif

#ifdef CONFIG_NANOMODBUS_SHELL_SERVER
int nmbs_z_shell_register_server(const char *name, void *server_ctx, nmbs_z_server_diag_cb diag_cb)
{
	if (!name || !server_ctx || !diag_cb) {
		return -EINVAL;
	}
	return -ENOSYS;
}

int nmbs_z_shell_unregister_server(const char *name)
{
	if (!name) {
		return -EINVAL;
	}
	return -ENOSYS;
}

#ifdef CONFIG_NANOMODBUS_SERVER_RTU
int nmbs_z_shell_register_server_rtu(const char *name, nmbs_z_server_rtu_t *server)
{
	if (!name || !server) {
		return -EINVAL;
	}
	return -ENOSYS;
}
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
int nmbs_z_shell_register_server_tcp(const char *name, nmbs_z_server_tcp_t *server)
{
	if (!name || !server) {
		return -EINVAL;
	}
	return -ENOSYS;
}
#endif
#endif
