/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_shell.h>
#include <errno.h>
#include <string.h>

#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT

struct client_entry {
	char name[NMBS_Z_SHELL_NAME_MAX_LEN];
	nmbs_z_client_t *client;
};

static struct client_entry s_clients[CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS];
static size_t s_client_count;

struct client_entry *nmbs_z_shell_find_client(const char *name)
{
	if (!name) {
		return NULL;
	}
	for (size_t i = 0; i < s_client_count; i++) {
		if (strcmp(s_clients[i].name, name) == 0) {
			return &s_clients[i];
		}
	}
	return NULL;
}

struct client_entry *nmbs_z_shell_get_default_client(void)
{
	if (s_client_count == 1) {
		return &s_clients[0];
	}
	return NULL;
}

int nmbs_z_shell_register_client(const char *name, nmbs_z_client_t *client)
{
	if (!name || !client) {
		return -EINVAL;
	}

	size_t len = strlen(name);
	if (len == 0 || len >= NMBS_Z_SHELL_NAME_MAX_LEN) {
		return -EINVAL;
	}

	if (nmbs_z_shell_find_client(name) != NULL) {
		return -EEXIST;
	}

	if (s_client_count >= CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS) {
		return -ENOMEM;
	}

	strncpy(s_clients[s_client_count].name, name, sizeof(s_clients[s_client_count].name) - 1);
	s_clients[s_client_count].name[sizeof(s_clients[s_client_count].name) - 1] = '\0';
	s_clients[s_client_count].client = client;
	s_client_count++;

	return 0;
}

int nmbs_z_shell_unregister_client(const char *name)
{
	if (!name) {
		return -EINVAL;
	}

	for (size_t i = 0; i < s_client_count; i++) {
		if (strcmp(s_clients[i].name, name) == 0) {
			for (size_t j = i; j + 1 < s_client_count; j++) {
				s_clients[j] = s_clients[j + 1];
			}
			memset(&s_clients[s_client_count - 1], 0, sizeof(s_clients[0]));
			s_client_count--;
			return 0;
		}
	}

	return -ENOENT;
}

#endif /* CONFIG_NANOMODBUS_SHELL_CLIENT */

#ifdef CONFIG_NANOMODBUS_SHELL_SERVER

struct server_entry {
	char name[NMBS_Z_SHELL_NAME_MAX_LEN];
	void *ctx;
	nmbs_z_server_diag_cb diag_cb;
};

static struct server_entry s_servers[CONFIG_NANOMODBUS_SHELL_MAX_SERVERS];
static size_t s_server_count;

struct server_entry *nmbs_z_shell_find_server(const char *name)
{
	if (!name) {
		return NULL;
	}
	for (size_t i = 0; i < s_server_count; i++) {
		if (strcmp(s_servers[i].name, name) == 0) {
			return &s_servers[i];
		}
	}
	return NULL;
}

struct server_entry *nmbs_z_shell_get_default_server(void)
{
	if (s_server_count == 1) {
		return &s_servers[0];
	}
	return NULL;
}

int nmbs_z_shell_register_server(const char *name, void *server_ctx, nmbs_z_server_diag_cb diag_cb)
{
	if (!name || !server_ctx || !diag_cb) {
		return -EINVAL;
	}

	size_t len = strlen(name);
	if (len == 0 || len >= NMBS_Z_SHELL_NAME_MAX_LEN) {
		return -EINVAL;
	}

	if (nmbs_z_shell_find_server(name) != NULL) {
		return -EEXIST;
	}

	if (s_server_count >= CONFIG_NANOMODBUS_SHELL_MAX_SERVERS) {
		return -ENOMEM;
	}

	strncpy(s_servers[s_server_count].name, name, sizeof(s_servers[s_server_count].name) - 1);
	s_servers[s_server_count].name[sizeof(s_servers[s_server_count].name) - 1] = '\0';
	s_servers[s_server_count].ctx = server_ctx;
	s_servers[s_server_count].diag_cb = diag_cb;
	s_server_count++;

	return 0;
}

int nmbs_z_shell_unregister_server(const char *name)
{
	if (!name) {
		return -EINVAL;
	}

	for (size_t i = 0; i < s_server_count; i++) {
		if (strcmp(s_servers[i].name, name) == 0) {
			for (size_t j = i; j + 1 < s_server_count; j++) {
				s_servers[j] = s_servers[j + 1];
			}
			memset(&s_servers[s_server_count - 1], 0, sizeof(s_servers[0]));
			s_server_count--;
			return 0;
		}
	}

	return -ENOENT;
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

#endif /* CONFIG_NANOMODBUS_SHELL_SERVER */
