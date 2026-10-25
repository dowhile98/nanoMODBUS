/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nanomodbus_z_tcp.h
 * @brief Modbus TCP transport adapter and dedicated Active Object server engine.
 */

#ifndef ZEPHYR_INCLUDE_NANOMODBUS_Z_TCP_H_
#define ZEPHYR_INCLUDE_NANOMODBUS_Z_TCP_H_

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#if !defined(CONFIG_NET_NAMESPACE_COMPAT_MODE)
#define ZEPHYR_INCLUDE_NET_COMPAT_MODE_SYMBOLS
#include <zephyr/net/net_compat.h>
#undef ZEPHYR_INCLUDE_NET_COMPAT_MODE_SYMBOLS
#endif

#include <nanomodbus.h>
#include "nanomodbus_z.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP Client Transport context.
 */
typedef struct nmbs_z_tcp_transport {
    int sock;
    struct sockaddr_storage server_addr;
    socklen_t addr_len;
    int32_t timeout_ms;
} nmbs_z_tcp_transport_t;

/**
 * @brief Connect TCP client transport to Modbus TCP server.
 *
 * @param transport Pointer to transport context.
 * @param server_ip IPv4 or IPv6 string address of target Modbus server.
 * @param port Target TCP port (standard 502).
 * @param timeout_ms Connection and read/write timeout in milliseconds.
 * @param platform_conf Out-parameter populated with nanoMODBUS platform callbacks.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_tcp_transport_connect(nmbs_z_tcp_transport_t *transport,
                                 const char *server_ip,
                                 uint16_t port,
                                 int32_t timeout_ms,
                                 nmbs_platform_conf *platform_conf);

/**
 * @brief Platform read callback for Modbus TCP transport.
 */
int32_t nmbs_z_transport_tcp_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/**
 * @brief Platform write callback for Modbus TCP transport.
 */
int32_t nmbs_z_transport_tcp_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/**
 * @brief Platform flush callback for Modbus TCP transport.
 */
void nmbs_z_transport_tcp_flush(nmbs_t *nmbs, void *arg);

/**
 * @brief Disconnect and close TCP client transport.
 *
 * @param transport Pointer to transport context.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_tcp_transport_close(nmbs_z_tcp_transport_t *transport);

#ifdef CONFIG_NANOMODBUS_SERVER_TCP

#ifndef CONFIG_NANOMODBUS_SERVER_TCP_STACK_SIZE
#define CONFIG_NANOMODBUS_SERVER_TCP_STACK_SIZE 2048
#endif

#ifndef CONFIG_NANOMODBUS_SERVER_TCP_PRIORITY
#define CONFIG_NANOMODBUS_SERVER_TCP_PRIORITY 7
#endif

/**
 * @brief Modbus TCP Server context instance.
 *
 * Embeds the listener thread stack, so instances must live in static storage.
 */
typedef struct nmbs_z_server_tcp {
    nmbs_t core;
    nmbs_callbacks callbacks;
    int listen_sock;
    int client_sock;
    uint16_t port;
    atomic_t running;
    atomic_t thread_active;
    struct k_thread thread;
    K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_NANOMODBUS_SERVER_TCP_STACK_SIZE);
    int priority;
    struct k_sem stop_sem;
    struct k_sem ready_sem;
    nmbs_z_tcp_transport_t client_transport;
} nmbs_z_server_tcp_t;

/**
 * @brief Initialize Modbus TCP Server context.
 *
 * The server serves one client connection at a time. Additional clients are
 * accepted only after the current connection is closed.
 *
 * @param server Pointer to uninitialized server context. It must remain valid
 *               and must not be placed on a stack that is destroyed while the
 *               server runs.
 * @param callbacks Modbus request handling callbacks.
 * @param port TCP port to listen on.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_init(nmbs_z_server_tcp_t *server,
                           const nmbs_callbacks *callbacks,
                           uint16_t port);

/**
 * @brief Start the Modbus TCP Server thread.
 *
 * @param server Pointer to server context.
 * @param callbacks Modbus request handling callbacks (if NULL, must have called nmbs_z_server_tcp_init).
 * @param port TCP port to bind (if 0, must have called nmbs_z_server_tcp_init).
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_start(nmbs_z_server_tcp_t *server,
                            const nmbs_callbacks *callbacks,
                            uint16_t port);

/**
 * @brief Stop the Modbus TCP Server thread and close listening and client sockets.
 *
 * @param server Pointer to server context.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_stop(nmbs_z_server_tcp_t *server);

#endif /* CONFIG_NANOMODBUS_SERVER_TCP */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_TCP_H_ */
