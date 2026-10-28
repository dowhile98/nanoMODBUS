/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_shell.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

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

int nmbs_z_shell_parse_u16(const struct shell *sh, const char *str, const char *param_name, uint16_t *val_out)
{
	if (!str || !val_out) {
		return -EINVAL;
	}

	char *endptr = NULL;
	errno = 0;
	unsigned long parsed = strtoul(str, &endptr, 0);

	if (*str == '\0' || *endptr != '\0' || errno == ERANGE) {
		if (sh) {
			shell_error(sh, "Invalid number format for %s: '%s'", param_name, str);
		}
		return -EINVAL;
	}

	if (parsed > 0xFFFF) {
		if (sh) {
			shell_error(sh, "%s overflow (>65535): '%s'", param_name, str);
		}
		return -ERANGE;
	}

	*val_out = (uint16_t)parsed;
	return 0;
}

int nmbs_z_shell_parse_quantity(const struct shell *sh, const char *str, uint16_t max_qty, uint16_t *qty_out)
{
	uint16_t qty = 0;
	int ret = nmbs_z_shell_parse_u16(sh, str, "quantity", &qty);

	if (ret != 0) {
		return ret;
	}

	if (qty < 1 || qty > max_qty) {
		if (sh) {
			shell_error(sh, "Quantity must be between 1 and %u (requested %u)", max_qty, qty);
		}
		return -EINVAL;
	}

	if (qty_out) {
		*qty_out = qty;
	}
	return 0;
}

int nmbs_z_shell_parse_bool(const struct shell *sh, const char *str, const char *param_name, bool *val_out)
{
	if (!str || !val_out) {
		return -EINVAL;
	}

	if (strcasecmp(str, "1") == 0 || strcasecmp(str, "on") == 0 ||
	    strcasecmp(str, "true") == 0 || strcasecmp(str, "high") == 0) {
		*val_out = true;
		return 0;
	}

	if (strcasecmp(str, "0") == 0 || strcasecmp(str, "off") == 0 ||
	    strcasecmp(str, "false") == 0 || strcasecmp(str, "low") == 0) {
		*val_out = false;
		return 0;
	}

	if (sh) {
		shell_error(sh, "Invalid boolean value for %s: '%s'. Expected on/off, true/false, 1/0, high/low",
			    param_name, str);
	}
	return -EINVAL;
}

int nmbs_z_shell_parse_options(const struct shell *sh, size_t argc, char **argv, struct nmbs_z_cli_options *opts)
{
	if (!argv || !opts) {
		return -EINVAL;
	}

	memset(opts, 0, sizeof(*opts));
	opts->timeout_ms = -1;

	for (size_t i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (strcmp(argv[i], "-c") == 0) {
				if (++i >= argc) {
					if (sh) {
						shell_error(sh, "Option -c requires an argument");
					}
					return -EINVAL;
				}
				opts->client_name = argv[i];
			} else if (strcmp(argv[i], "-s") == 0) {
				if (++i >= argc) {
					if (sh) {
						shell_error(sh, "Option -s requires an argument");
					}
					return -EINVAL;
				}
				opts->server_name = argv[i];
			} else if (strcmp(argv[i], "-u") == 0) {
				if (++i >= argc) {
					if (sh) {
						shell_error(sh, "Option -u requires an argument");
					}
					return -EINVAL;
				}
				char *endptr = NULL;
				unsigned long u = strtoul(argv[i], &endptr, 0);

				if (*argv[i] == '\0' || *endptr != '\0' || u > 247) {
					if (sh) {
						shell_error(sh, "Invalid unit ID: '%s'", argv[i]);
					}
					return -EINVAL;
				}
				opts->unit_id = (uint8_t)u;
				opts->unit_id_specified = true;
			} else if (strcmp(argv[i], "-t") == 0) {
				if (++i >= argc) {
					if (sh) {
						shell_error(sh, "Option -t requires an argument");
					}
					return -EINVAL;
				}
				char *endptr = NULL;
				long t = strtol(argv[i], &endptr, 0);

				if (*argv[i] == '\0' || *endptr != '\0' || t < 0) {
					if (sh) {
						shell_error(sh, "Invalid timeout: '%s'", argv[i]);
					}
					return -EINVAL;
				}
				opts->timeout_ms = (int32_t)t;
			} else {
				if (sh) {
					shell_error(sh, "Unknown option: %s", argv[i]);
				}
				return -EINVAL;
			}
		} else {
			if (opts->positional_count >= ARRAY_SIZE(opts->positional_idx)) {
				if (sh) {
					shell_error(sh, "Too many positional arguments");
				}
				return -EINVAL;
			}
			opts->positional_idx[opts->positional_count++] = i;
		}
	}

	return 0;
}
