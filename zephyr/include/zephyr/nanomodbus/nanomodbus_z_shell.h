/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nanomodbus_z_shell.h
 * @brief Public interface and registry contracts for nanoMODBUS Zephyr Shell integration.
 * @ingroup nanomodbus_zephyr
 */

#ifndef ZEPHYR_INCLUDE_NANOMODBUS_Z_SHELL_H_
#define ZEPHYR_INCLUDE_NANOMODBUS_Z_SHELL_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>

#ifdef CONFIG_NANOMODBUS_SERVER_RTU
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length for a registered shell instance name, including NUL terminator. */
#define NMBS_Z_SHELL_NAME_MAX_LEN 16

/**
 * @brief Modbus server operational state.
 */
enum nmbs_z_server_state {
    NMBS_Z_SERVER_STATE_STOPPED = 0, /**< Server engine is inactive */
    NMBS_Z_SERVER_STATE_RUNNING = 1, /**< Server engine is actively listening/polling */
    NMBS_Z_SERVER_STATE_ERROR = 2    /**< Server encountered an unrecoverable fault */
};

/**
 * @brief Server diagnostic statistics counters.
 *
 * All counters are monotonically increasing 32-bit unsigned integers.
 */
struct nmbs_z_server_stats {
    uint32_t rx_frames;        /**< Total Modbus request frames received */
    uint32_t tx_frames;        /**< Total Modbus response frames transmitted */
    uint32_t rx_errors;        /**< Checksum (CRC), framing, or framing silence errors */
    uint32_t exceptions_sent;  /**< Modbus exception responses sent to clients */
    uint32_t requests_handled; /**< Valid Modbus requests successfully processed */
};

/**
 * @brief Server information and diagnostic snapshot.
 */
struct nmbs_z_server_info {
    enum nmbs_z_server_state state;   /**< Operational state of the server */
    uint8_t unit_id;                  /**< Configured slave/unit address (RTU) or 0 (TCP) */
    uint16_t port;                    /**< TCP port (if TCP server) or 0 (RTU) */
    struct nmbs_z_server_stats stats; /**< Diagnostic counters snapshot */
};

/**
 * @brief Diagnostic query callback for Modbus server instances.
 *
 * @param server_ctx Opaque pointer to the server instance context.
 * @param info Output pointer to receive server operational info and statistics.
 * @param reset If true, statistics counters are atomically reset to zero after reading.
 * @return 0 on success, negative errno on failure.
 */
typedef int (*nmbs_z_server_diag_cb)(void* server_ctx, struct nmbs_z_server_info* info, bool reset);

#ifdef CONFIG_NANOMODBUS_SHELL_CLIENT

/**
 * @brief Register a nanoMODBUS client instance with the shell CLI.
 *
 * Makes the client available for interactive shell commands. If this is the
 * first or only client registered, it becomes the default target when the
 * `-c <client>` option is omitted in CLI commands.
 *
 * @param name Unique human-readable name for the client (e.g., "rtu0", "tcp_ext").
 *             Must not exceed NMBS_Z_SHELL_NAME_MAX_LEN - 1 characters.
 * @param client Pointer to initialized nmbs_z_client_t instance. Must remain valid
 *               for the entire duration of registration.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name or @p client is NULL, or @p name is empty/too long.
 * @retval -EEXIST if a client with @p name is already registered.
 * @retval -ENOMEM if the client registry is full (CONFIG_NANOMODBUS_SHELL_MAX_CLIENTS).
 */
int nmbs_z_shell_register_client(const char* name, nmbs_z_client_t* client);

/**
 * @brief Unregister a nanoMODBUS client instance from the shell CLI.
 *
 * @param name Name of the registered client instance to remove.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name is NULL.
 * @retval -ENOENT if no client matching @p name was found in the registry.
 */
int nmbs_z_shell_unregister_client(const char* name);

#endif /* CONFIG_NANOMODBUS_SHELL_CLIENT */

#ifdef CONFIG_NANOMODBUS_SHELL_SERVER

/**
 * @brief Register a generic Modbus server instance with custom diagnostic callback.
 *
 * @param name Unique human-readable name for the server (e.g., "srv_rtu", "srv_tcp").
 *             Must not exceed NMBS_Z_SHELL_NAME_MAX_LEN - 1 characters.
 * @param server_ctx Opaque pointer to server instance.
 * @param diag_cb Callback invoked by `nmbs status` and `nmbs reset-stats`.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name, @p server_ctx, or @p diag_cb is NULL.
 * @retval -EEXIST if a server with @p name is already registered.
 * @retval -ENOMEM if the server registry is full (CONFIG_NANOMODBUS_SHELL_MAX_SERVERS).
 */
int nmbs_z_shell_register_server(const char* name, void* server_ctx, nmbs_z_server_diag_cb diag_cb);

/**
 * @brief Unregister a Modbus server instance from the shell CLI.
 *
 * @param name Name of the registered server instance to remove.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p name is NULL.
 * @retval -ENOENT if no server matching @p name was found in the registry.
 */
int nmbs_z_shell_unregister_server(const char* name);

#ifdef CONFIG_NANOMODBUS_SERVER_RTU
/**
 * @brief Helper to register a standard Modbus RTU Server with built-in diagnostics.
 *
 * @param name Unique name for the RTU server instance (e.g., "rtu_srv").
 * @param server Pointer to initialized nmbs_z_server_rtu_t instance.
 *
 * @retval 0 on success, negative errno on failure.
 */
int nmbs_z_shell_register_server_rtu(const char* name, nmbs_z_server_rtu_t* server);
#endif

#ifdef CONFIG_NANOMODBUS_SERVER_TCP
/**
 * @brief Helper to register a standard Modbus TCP Server with built-in diagnostics.
 *
 * @param name Unique name for the TCP server instance (e.g., "tcp_srv").
 * @param server Pointer to initialized nmbs_z_server_tcp_t instance.
 *
 * @retval 0 on success, negative errno on failure.
 */
int nmbs_z_shell_register_server_tcp(const char* name, nmbs_z_server_tcp_t* server);
#endif

#endif /* CONFIG_NANOMODBUS_SHELL_SERVER */

struct shell;

/**
 * @brief Command line parsed options and positional argument tracking.
 */
struct nmbs_z_cli_options {
    const char* client_name;
    const char* server_name;
    uint8_t unit_id;
    bool unit_id_specified;
    int32_t timeout_ms;
    size_t positional_idx[128];
    size_t positional_count;
};

int nmbs_z_shell_parse_options(const struct shell* sh, size_t argc, char** argv, struct nmbs_z_cli_options* opts);
int nmbs_z_shell_parse_u16(const struct shell* sh, const char* str, const char* param_name, uint16_t* val_out);
int nmbs_z_shell_parse_bool(const struct shell* sh, const char* str, const char* param_name, bool* val_out);
int nmbs_z_shell_parse_quantity(const struct shell* sh, const char* str, uint16_t max_qty, uint16_t* qty_out);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_SHELL_H_ */
