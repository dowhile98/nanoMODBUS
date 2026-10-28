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

#include "nanomodbus_z.h"
#include <nanomodbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP Client Transport context.
 *
 * Address fields use the native Zephyr types (net_sockaddr_storage,
 * net_socklen_t) so the header does not have to pull in the POSIX
 * compatibility namespace.
 */
typedef struct nmbs_z_tcp_transport {
    int sock;
    struct net_sockaddr_storage server_addr;
    net_socklen_t addr_len;
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
int nmbs_z_tcp_transport_connect(nmbs_z_tcp_transport_t* transport, const char* server_ip, uint16_t port,
                                 int32_t timeout_ms, nmbs_platform_conf* platform_conf);

/**
 * @brief Platform read callback for Modbus TCP transport.
 */
int32_t nmbs_z_transport_tcp_read(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg);

/**
 * @brief Platform write callback for Modbus TCP transport.
 */
int32_t nmbs_z_transport_tcp_write(const uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg);

/**
 * @brief Platform flush callback for Modbus TCP transport.
 */
void nmbs_z_transport_tcp_flush(nmbs_t* nmbs, void* arg);

/**
 * @brief Disconnect and close TCP client transport.
 *
 * @param transport Pointer to transport context.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_tcp_transport_close(nmbs_z_tcp_transport_t* transport);

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
    struct nmbs_z_stats stats; /**< Server diagnostic statistics */
} nmbs_z_server_tcp_t;

/**
 * @brief Initialize Modbus TCP Server context.
 *
 * Concurrency contract: an instance serves exactly one client connection at a
 * time. While a client is being served, further connection attempts stay in
 * the listen backlog (CONFIG_NANOMODBUS_SERVER_TCP_BACKLOG) and are accepted
 * only after the current client disconnects. This keeps the RAM cost at one
 * core, one transport and one thread per server instance.
 *
 * To serve several clients concurrently, instantiate one
 * nmbs_z_server_tcp_t per desired connection on different ports; each
 * instance owns its own stack, core state and transport.
 *
 * @param server Pointer to uninitialized server context. It must remain valid
 *               and be in static storage, because it embeds the listener
 *               thread stack.
 * @param callbacks Modbus request handling callbacks.
 * @param port TCP port to listen on.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_init(nmbs_z_server_tcp_t* server, const nmbs_callbacks* callbacks, uint16_t port);

/**
 * @brief Start the Modbus TCP Server thread.
 *
 * Either pass @p callbacks and @p port to (re)initialize the instance, or pass
 * NULL and 0 to start an instance already configured with
 * nmbs_z_server_tcp_init().
 *
 * @param server Pointer to server context.
 * @param callbacks Modbus request handling callbacks, or NULL to reuse the
 *                  ones passed to nmbs_z_server_tcp_init().
 * @param port TCP port to bind, or 0 to reuse the initialized port.
 * @return 0 on success, -EALREADY if the server is already running, other
 *         negative errno on failure.
 */
int nmbs_z_server_tcp_start(nmbs_z_server_tcp_t* server, const nmbs_callbacks* callbacks, uint16_t port);

/**
 * @brief Stop the Modbus TCP Server thread and close listening and client sockets.
 *
 * @param server Pointer to server context.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_stop(nmbs_z_server_tcp_t* server);

/**
 * @brief Retrieve runtime diagnostic statistics from a TCP Server instance.
 *
 * @param server Pointer to server instance.
 * @param stats Pointer to output statistics structure.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_get_stats(const nmbs_z_server_tcp_t* server, struct nmbs_z_stats* stats);

/**
 * @brief Reset runtime diagnostic statistics on a TCP Server instance.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_reset_stats(nmbs_z_server_tcp_t* server);

#endif /* CONFIG_NANOMODBUS_SERVER_TCP */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_TCP_H_ */
