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
#include <stdio.h>

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
static int rtu_server_diag_cb(void *server_ctx, struct nmbs_z_server_info *info, bool reset)
{
	nmbs_z_server_rtu_t *server = (nmbs_z_server_rtu_t *)server_ctx;

	if (!server) {
		return -EINVAL;
	}

	if (info != NULL) {
		info->state = atomic_get(&server->running) ? NMBS_Z_SERVER_STATE_RUNNING : NMBS_Z_SERVER_STATE_STOPPED;
		info->unit_id = server->server_address;
		info->port = 0;
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
		struct nmbs_z_stats raw_stats;
		int ret = nmbs_z_server_rtu_get_stats(server, &raw_stats);
		if (ret == 0) {
			info->stats.rx_frames = raw_stats.rx_frames;
			info->stats.tx_frames = raw_stats.tx_frames;
			info->stats.rx_errors = raw_stats.crc_errors + raw_stats.timeouts;
			info->stats.exceptions_sent = raw_stats.exceptions;
			if (raw_stats.rx_frames >= info->stats.rx_errors) {
				info->stats.requests_handled = raw_stats.rx_frames - info->stats.rx_errors;
			} else {
				info->stats.requests_handled = 0;
			}
		} else {
			memset(&info->stats, 0, sizeof(info->stats));
		}
#else
		memset(&info->stats, 0, sizeof(info->stats));
#endif
	}

	if (reset) {
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
		(void)nmbs_z_server_rtu_reset_stats(server);
#endif
	}

	return 0;
}

int nmbs_z_shell_register_server_rtu(const char *name, nmbs_z_server_rtu_t *server)
{
	if (!name || !server) {
		return -EINVAL;
	}
	return nmbs_z_shell_register_server(name, (void *)server, rtu_server_diag_cb);
}
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
static int tcp_server_diag_cb(void *server_ctx, struct nmbs_z_server_info *info, bool reset)
{
	nmbs_z_server_tcp_t *server = (nmbs_z_server_tcp_t *)server_ctx;

	if (!server) {
		return -EINVAL;
	}

	if (info != NULL) {
		info->state = atomic_get(&server->running) ? NMBS_Z_SERVER_STATE_RUNNING : NMBS_Z_SERVER_STATE_STOPPED;
		info->unit_id = 0;
		info->port = server->port;
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
		struct nmbs_z_stats raw_stats;
		int ret = nmbs_z_server_tcp_get_stats(server, &raw_stats);
		if (ret == 0) {
			info->stats.rx_frames = raw_stats.rx_frames;
			info->stats.tx_frames = raw_stats.tx_frames;
			info->stats.rx_errors = raw_stats.crc_errors + raw_stats.timeouts;
			info->stats.exceptions_sent = raw_stats.exceptions;
			if (raw_stats.rx_frames >= info->stats.rx_errors) {
				info->stats.requests_handled = raw_stats.rx_frames - info->stats.rx_errors;
			} else {
				info->stats.requests_handled = 0;
			}
		} else {
			memset(&info->stats, 0, sizeof(info->stats));
		}
#else
		memset(&info->stats, 0, sizeof(info->stats));
#endif
	}

	if (reset) {
#ifdef CONFIG_NANOMODBUS_DIAGNOSTICS
		(void)nmbs_z_server_tcp_reset_stats(server);
#endif
	}

	return 0;
}

int nmbs_z_shell_register_server_tcp(const char *name, nmbs_z_server_tcp_t *server)
{
	if (!name || !server) {
		return -EINVAL;
	}
	return nmbs_z_shell_register_server(name, (void *)server, tcp_server_diag_cb);
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

#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT

static void format_registers_table(const struct shell *sh, uint16_t start_addr, const uint16_t *regs, uint16_t count)
{
	char line_buf[128];

	for (uint16_t i = 0; i < count; i += 4) {
		uint16_t addr = start_addr + i;
		int len = snprintf(line_buf, sizeof(line_buf), "[0x%04X / %05u]:", addr, addr);

		for (uint16_t j = 0; j < 4 && (i + j) < count; j++) {
			uint16_t val = regs[i + j];
			len += snprintf(line_buf + len, sizeof(line_buf) - len, " %6u (0x%04X)", val, val);
		}
		shell_print(sh, "%s", line_buf);
	}
}

static void format_coils_bitfield(const struct shell *sh, uint16_t start_addr, const nmbs_bitfield coils, uint16_t count)
{
	char line_buf[128];

	for (uint16_t i = 0; i < count; i += 8) {
		uint16_t addr = start_addr + i;
		int len = snprintf(line_buf, sizeof(line_buf), "[0x%04X / %05u]:", addr, addr);
		uint8_t byte_val = 0;

		for (uint16_t j = 0; j < 8 && (i + j) < count; j++) {
			bool b = nmbs_bitfield_read(coils, i + j);
			if (b) {
				byte_val |= (1U << j);
			}
			len += snprintf(line_buf + len, sizeof(line_buf) - len, "  %c", b ? '1' : '0');
		}

		uint16_t printed = ((count - i) < 8) ? (count - i) : 8;
		for (uint16_t pad = printed; pad < 8; pad++) {
			len += snprintf(line_buf + len, sizeof(line_buf) - len, "   ");
		}

		snprintf(line_buf + len, sizeof(line_buf) - len, "  (0x%02X)", byte_val);
		shell_print(sh, "%s", line_buf);
	}
}

static struct client_entry *resolve_client(const struct shell *sh, const struct nmbs_z_cli_options *opts)
{
	if (opts->client_name) {
		struct client_entry *entry = nmbs_z_shell_find_client(opts->client_name);
		if (!entry) {
			shell_error(sh, "Client '%s' not found. Run 'nmbs list'", opts->client_name);
			return NULL;
		}
		return entry;
	}

	if (s_client_count == 0) {
		shell_error(sh, "No Modbus client registered.");
		return NULL;
	}

	if (s_client_count > 1) {
		shell_error(sh, "Multiple clients registered. Specify target with -c <client_name>.");
		return NULL;
	}

	return &s_clients[0];
}

static int report_client_error(const struct shell *sh, nmbs_z_client_t *client, int ret)
{
	nmbs_error raw = nmbs_z_client_get_last_error(client);

	if (ret == -EBUSY || (ret == -ETIMEDOUT && raw != NMBS_ERROR_TIMEOUT)) {
		shell_error(sh, "Client busy: Lock timeout expired");
	} else if (nmbs_error_is_exception(raw)) {
		shell_error(sh, "Modbus Exception: %s (Code 0x%02X)", nmbs_z_strerror(raw), (uint8_t)raw);
	} else {
		shell_error(sh, "Communication Error: %s (errno %d)", nmbs_z_strerror(raw), ret);
	}
	return ret;
}

static int cmd_nmbs_read_holding(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs read-holding [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t qty = 0;
	ret = nmbs_z_shell_parse_quantity(sh, argv[opts.positional_idx[1]], CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty);
	if (ret != 0) {
		return ret;
	}

	uint16_t data[CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS];
	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_read_holding_registers_addr(entry->client, opts.unit_id, addr, qty, data, timeout);
	} else {
		ret = nmbs_z_client_read_holding_registers(entry->client, addr, qty, data, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	format_registers_table(sh, addr, data, qty);
	return 0;
}

static int cmd_nmbs_read_input(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs read-input [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t qty = 0;
	ret = nmbs_z_shell_parse_quantity(sh, argv[opts.positional_idx[1]], CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS, &qty);
	if (ret != 0) {
		return ret;
	}

	uint16_t data[CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS];
	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_read_input_registers_addr(entry->client, opts.unit_id, addr, qty, data, timeout);
	} else {
		ret = nmbs_z_client_read_input_registers(entry->client, addr, qty, data, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	format_registers_table(sh, addr, data, qty);
	return 0;
}

static int cmd_nmbs_read_coils(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs read-coils [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t max_coils = CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS * 16;
	if (max_coils > 2000) {
		max_coils = 2000;
	}
	uint16_t qty = 0;
	ret = nmbs_z_shell_parse_quantity(sh, argv[opts.positional_idx[1]], max_coils, &qty);
	if (ret != 0) {
		return ret;
	}

	nmbs_bitfield coils;
	memset(coils, 0, sizeof(coils));
	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_read_coils_addr(entry->client, opts.unit_id, addr, qty, coils, timeout);
	} else {
		ret = nmbs_z_client_read_coils(entry->client, addr, qty, coils, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	format_coils_bitfield(sh, addr, coils, qty);
	return 0;
}

static int cmd_nmbs_read_discrete(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs read-discrete [-c <client>] [-u <unit>] [-t <ms>] <addr> <qty>");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t max_inputs = CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS * 16;
	if (max_inputs > 2000) {
		max_inputs = 2000;
	}
	uint16_t qty = 0;
	ret = nmbs_z_shell_parse_quantity(sh, argv[opts.positional_idx[1]], max_inputs, &qty);
	if (ret != 0) {
		return ret;
	}

	nmbs_bitfield inputs;
	memset(inputs, 0, sizeof(inputs));
	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_read_discrete_inputs_addr(entry->client, opts.unit_id, addr, qty, inputs, timeout);
	} else {
		ret = nmbs_z_client_read_discrete_inputs(entry->client, addr, qty, inputs, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	format_coils_bitfield(sh, addr, inputs, qty);
	return 0;
}

static int cmd_nmbs_write_coil(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs write-coil [-c <client>] [-u <unit>] [-t <ms>] <addr> <val>");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	bool val = false;
	ret = nmbs_z_shell_parse_bool(sh, argv[opts.positional_idx[1]], "value", &val);
	if (ret != 0) {
		return ret;
	}

	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_write_single_coil_addr(entry->client, opts.unit_id, addr, val, timeout);
	} else {
		ret = nmbs_z_client_write_single_coil(entry->client, addr, val, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	uint8_t unit = opts.unit_id_specified ? opts.unit_id : entry->client->core.dest_address_rtu;
	if (unit == 0 && !opts.unit_id_specified) {
		unit = 1;
	}
	shell_print(sh, "OK: Wrote coil at address 0x%04X (%u) = %s on client '%s' (unit %u)",
		    addr, addr, val ? "ON" : "OFF", entry->name, unit);
	return 0;
}

static int cmd_nmbs_write_register(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs write-register [-c <client>] [-u <unit>] [-t <ms>] <addr> <val>");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t val = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[1]], "value", &val);
	if (ret != 0) {
		return ret;
	}

	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_write_single_register_addr(entry->client, opts.unit_id, addr, val, timeout);
	} else {
		ret = nmbs_z_client_write_single_register(entry->client, addr, val, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	uint8_t unit = opts.unit_id_specified ? opts.unit_id : entry->client->core.dest_address_rtu;
	if (unit == 0 && !opts.unit_id_specified) {
		unit = 1;
	}
	shell_print(sh, "OK: Wrote register at address 0x%04X (%u) = %u (0x%04X) on client '%s' (unit %u)",
		    addr, addr, val, val, entry->name, unit);
	return 0;
}

static int cmd_nmbs_write_coils(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs write-coils [-c <client>] [-u <unit>] [-t <ms>] <addr> <val0> [val1...]");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t qty = (uint16_t)(opts.positional_count - 1);
	uint16_t max_coils = CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS * 16;
	if (max_coils > 2000) {
		max_coils = 2000;
	}
	if (qty > max_coils) {
		shell_error(sh, "Too many coils: %u (max %u)", qty, max_coils);
		return -EINVAL;
	}

	nmbs_bitfield coils;
	memset(coils, 0, sizeof(coils));

	for (uint16_t i = 0; i < qty; i++) {
		bool b = false;
		ret = nmbs_z_shell_parse_bool(sh, argv[opts.positional_idx[1 + i]], "coil value", &b);
		if (ret != 0) {
			return ret;
		}
		nmbs_bitfield_write(coils, i, b);
	}

	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_write_multiple_coils_addr(entry->client, opts.unit_id, addr, qty, coils, timeout);
	} else {
		ret = nmbs_z_client_write_multiple_coils(entry->client, addr, qty, coils, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	uint8_t unit = opts.unit_id_specified ? opts.unit_id : entry->client->core.dest_address_rtu;
	if (unit == 0 && !opts.unit_id_specified) {
		unit = 1;
	}
	shell_print(sh, "OK: Wrote %u coil(s) starting at address 0x%04X (%u) on client '%s' (unit %u)",
		    qty, addr, addr, entry->name, unit);
	return 0;
}

static int cmd_nmbs_write_registers(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	if (opts.positional_count < 2) {
		shell_error(sh, "Usage: nmbs write-registers [-c <client>] [-u <unit>] [-t <ms>] <addr> <val0> [val1...]");
		return -EINVAL;
	}

	struct client_entry *entry = resolve_client(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	uint16_t addr = 0;
	ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[0]], "address", &addr);
	if (ret != 0) {
		return ret;
	}

	uint16_t qty = (uint16_t)(opts.positional_count - 1);
	if (qty > CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS) {
		shell_error(sh, "Too many registers: %u (max %u)", qty, CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS);
		return -EINVAL;
	}

	uint16_t data[CONFIG_NANOMODBUS_SHELL_MAX_DATA_WORDS];

	for (uint16_t i = 0; i < qty; i++) {
		ret = nmbs_z_shell_parse_u16(sh, argv[opts.positional_idx[1 + i]], "register value", &data[i]);
		if (ret != 0) {
			return ret;
		}
	}

	k_timeout_t timeout = (opts.timeout_ms >= 0) ? K_MSEC(opts.timeout_ms) : K_MSEC(1000);

	if (opts.unit_id_specified) {
		ret = nmbs_z_client_write_multiple_registers_addr(entry->client, opts.unit_id, addr, qty, data, timeout);
	} else {
		ret = nmbs_z_client_write_multiple_registers(entry->client, addr, qty, data, timeout);
	}

	if (ret < 0) {
		return report_client_error(sh, entry->client, ret);
	}

	uint8_t unit = opts.unit_id_specified ? opts.unit_id : entry->client->core.dest_address_rtu;
	if (unit == 0 && !opts.unit_id_specified) {
		unit = 1;
	}
	shell_print(sh, "OK: Wrote %u register(s) starting at address 0x%04X (%u) on client '%s' (unit %u)",
		    qty, addr, addr, entry->name, unit);
	return 0;
}

#endif /* CONFIG_NANOMODBUS_SHELL_CLIENT */

#ifdef CONFIG_NANOMODBUS_SHELL_SERVER

static struct server_entry *resolve_server(const struct shell *sh, const struct nmbs_z_cli_options *opts)
{
	if (opts->server_name) {
		struct server_entry *entry = nmbs_z_shell_find_server(opts->server_name);
		if (!entry) {
			shell_error(sh, "Server '%s' not found. Run 'nmbs list'", opts->server_name);
			return NULL;
		}
		return entry;
	}

	if (s_server_count == 0) {
		shell_error(sh, "No Modbus server registered.");
		return NULL;
	}

	if (s_server_count > 1) {
		shell_error(sh, "Multiple servers registered. Specify target with -s <server_name>.");
		return NULL;
	}

	return &s_servers[0];
}

static int cmd_nmbs_status(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	struct server_entry *entry = resolve_server(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	struct nmbs_z_server_info info;
	memset(&info, 0, sizeof(info));
	ret = entry->diag_cb(entry->ctx, &info, false);
	if (ret != 0) {
		shell_error(sh, "Failed to retrieve status for server '%s': %d", entry->name, ret);
		return ret;
	}

	const char *state_str = "UNKNOWN";
	switch (info.state) {
	case NMBS_Z_SERVER_STATE_STOPPED:
		state_str = "STOPPED";
		break;
	case NMBS_Z_SERVER_STATE_RUNNING:
		state_str = "RUNNING";
		break;
	case NMBS_Z_SERVER_STATE_ERROR:
		state_str = "ERROR";
		break;
	}

	shell_print(sh, "Modbus Server '%s' Status:", entry->name);
	shell_print(sh, "  Operational State:  %s", state_str);
	if (info.port > 0) {
		shell_print(sh, "  TCP Port:           %u", info.port);
	} else {
		shell_print(sh, "  Modbus Unit ID:     %u", info.unit_id);
	}
	shell_print(sh, "  Frames Received:    %u", info.stats.rx_frames);
	shell_print(sh, "  Frames Transmitted: %u", info.stats.tx_frames);
	shell_print(sh, "  Checksum Errors:    %u", info.stats.rx_errors);
	shell_print(sh, "  Exceptions Sent:    %u", info.stats.exceptions_sent);
	shell_print(sh, "  Requests Handled:   %u", info.stats.requests_handled);

	return 0;
}

static int cmd_nmbs_reset_stats(const struct shell *sh, size_t argc, char **argv)
{
	struct nmbs_z_cli_options opts;
	int ret = nmbs_z_shell_parse_options(sh, argc, argv, &opts);
	if (ret != 0) {
		return ret;
	}

	struct server_entry *entry = resolve_server(sh, &opts);
	if (!entry) {
		return -ENODEV;
	}

	struct nmbs_z_server_info info;
	memset(&info, 0, sizeof(info));
	ret = entry->diag_cb(entry->ctx, &info, true);
	if (ret != 0) {
		shell_error(sh, "Failed to reset stats for server '%s': %d", entry->name, ret);
		return ret;
	}

	shell_print(sh, "OK: Diagnostic counters for server '%s' reset to 0.", entry->name);
	return 0;
}

#endif /* CONFIG_NANOMODBUS_SHELL_SERVER */

static int cmd_nmbs_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "nanoMODBUS Registered Instances:");

	shell_print(sh, "Clients:");
#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT
	if (s_client_count == 0) {
		shell_print(sh, "  (none)");
	} else {
		for (size_t i = 0; i < s_client_count; i++) {
			bool is_default = (s_client_count == 1);
			uint8_t unit = s_clients[i].client->core.dest_address_rtu;
			if (unit == 0) {
				unit = 1;
			}
			int32_t to = s_clients[i].client->core.read_timeout_ms;
			int32_t bto = s_clients[i].client->core.byte_timeout_ms;
			shell_print(sh, "  [%u] '%s'%s -> Target Unit: %u, Timeout: %d ms, Byte Timeout: %d ms",
				    (unsigned int)i, s_clients[i].name,
				    is_default ? " (default)" : "",
				    unit, to, bto);
		}
	}
#else
	shell_print(sh, "  (disabled)");
#endif

	shell_print(sh, "Servers:");
#ifdef CONFIG_NANOMODBUS_SHELL_SERVER
	if (s_server_count == 0) {
		shell_print(sh, "  (none)");
	} else {
		for (size_t i = 0; i < s_server_count; i++) {
			bool is_default = (s_server_count == 1);
			struct nmbs_z_server_info info;
			memset(&info, 0, sizeof(info));
			int ret = s_servers[i].diag_cb(s_servers[i].ctx, &info, false);
			const char *state_str = "UNKNOWN";
			if (ret == 0) {
				switch (info.state) {
				case NMBS_Z_SERVER_STATE_STOPPED:
					state_str = "STOPPED";
					break;
				case NMBS_Z_SERVER_STATE_RUNNING:
					state_str = "RUNNING";
					break;
				case NMBS_Z_SERVER_STATE_ERROR:
					state_str = "ERROR";
					break;
				}
			}
			if (info.port > 0) {
				shell_print(sh, "  [%u] '%s'%s -> Type: TCP, Port: %u, State: %s",
					    (unsigned int)i, s_servers[i].name,
					    is_default ? " (default)" : "",
					    info.port, state_str);
			} else {
				shell_print(sh, "  [%u] '%s'%s -> Type: RTU, Address: %u, State: %s",
					    (unsigned int)i, s_servers[i].name,
					    is_default ? " (default)" : "",
					    info.unit_id, state_str);
			}
		}
	}
#else
	shell_print(sh, "  (disabled)");
#endif

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_nmbs,
	SHELL_CMD_ARG(list, NULL, "List registered Modbus instances", cmd_nmbs_list, 1, 0),
#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT
	SHELL_CMD_ARG(read-coils, NULL, "Read coils (FC 01)", cmd_nmbs_read_coils, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(read-discrete, NULL, "Read discrete inputs (FC 02)", cmd_nmbs_read_discrete, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(read-holding, NULL, "Read holding registers (FC 03)", cmd_nmbs_read_holding, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(read-input, NULL, "Read input registers (FC 04)", cmd_nmbs_read_input, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(write-coil, NULL, "Write single coil (FC 05)", cmd_nmbs_write_coil, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(write-register, NULL, "Write single register (FC 06)", cmd_nmbs_write_register, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(write-coils, NULL, "Write multiple coils (FC 15)", cmd_nmbs_write_coils, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(write-registers, NULL, "Write multiple registers (FC 16)", cmd_nmbs_write_registers, 3, SHELL_OPT_ARG_CHECK_SKIP),
#endif
#ifdef CONFIG_NANOMODBUS_SHELL_SERVER
	SHELL_CMD_ARG(reset-stats, NULL, "Reset server diagnostic counters to 0", cmd_nmbs_reset_stats, 1, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD_ARG(status, NULL, "Display server operational status and diagnostics", cmd_nmbs_status, 1, SHELL_OPT_ARG_CHECK_SKIP),
#endif
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(nmbs, &sub_nmbs, "nanoMODBUS commands", NULL);


