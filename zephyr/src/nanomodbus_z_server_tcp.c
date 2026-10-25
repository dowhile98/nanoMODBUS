/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/logging/log.h>
#include <string.h>

#ifdef CONFIG_NANOMODBUS_SERVER_TCP

LOG_MODULE_REGISTER(nmbs_server_tcp, CONFIG_NANOMODBUS_LOG_LEVEL);

static void tcp_server_thread_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    nmbs_z_server_tcp_t *server = (nmbs_z_server_tcp_t *)arg1;

    server->listen_sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listen_sock < 0) {
        LOG_ERR("Failed to create listen socket: %d", errno);
        atomic_set(&server->running, 0);
        k_sem_give(&server->ready_sem);
        k_sem_give(&server->stop_sem);
        return;
    }

    int opt = 1;
    int ret = zsock_setsockopt(server->listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret < 0) {
        LOG_WRN("SO_REUSEADDR setsockopt failed: %d", errno);
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server->port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    ret = zsock_bind(server->listen_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0) {
        LOG_ERR("Failed to bind socket on port %d: %d", server->port, errno);
        zsock_close(server->listen_sock);
        server->listen_sock = -1;
        atomic_set(&server->running, 0);
        k_sem_give(&server->ready_sem);
        k_sem_give(&server->stop_sem);
        return;
    }

    ret = zsock_listen(server->listen_sock, CONFIG_NANOMODBUS_SERVER_TCP_BACKLOG);
    if (ret < 0) {
        LOG_ERR("Failed to listen on socket: %d", errno);
        zsock_close(server->listen_sock);
        server->listen_sock = -1;
        atomic_set(&server->running, 0);
        k_sem_give(&server->ready_sem);
        k_sem_give(&server->stop_sem);
        return;
    }

    /* Signal server thread is successfully listening and ready */
    k_sem_give(&server->ready_sem);
    LOG_INF("Modbus TCP Server listening on port %d", server->port);

    while (atomic_get(&server->running)) {
        struct zsock_pollfd pfd = {
            .fd = server->listen_sock,
            .events = ZSOCK_POLLIN,
        };

        int pret = zsock_poll(&pfd, 1, 200);
        if (pret < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERR("Poll error on listen socket: %d", errno);
            break;
        }

        if (pret == 0) {
            /* Timeout: cycle to check atomic running flag */
            continue;
        }

        if (pfd.revents & ZSOCK_POLLIN) {
            struct sockaddr_storage client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_sock = zsock_accept(server->listen_sock,
                                          (struct sockaddr *)&client_addr,
                                          &client_addr_len);
            if (client_sock < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                LOG_WRN("Accept failed: %d", errno);
                continue;
            }

            server->client_sock = client_sock;
            server->client_transport.sock = client_sock;
            server->client_transport.timeout_ms = CONFIG_NANOMODBUS_SERVER_TCP_TIMEOUT_MS;

            /* Request loop with client */
            while (atomic_get(&server->running)) {
                nmbs_error err = nmbs_server_poll(&server->core);

                if (err == NMBS_ERROR_NONE || err == NMBS_ERROR_TIMEOUT) {
                    /* Idle connection, or an incomplete frame that the core
                     * already discarded. Keep the connection open.
                     */
                    continue;
                }

                LOG_DBG("Client disconnected or error (%d), returning to accept", err);
                break;
            }

            /* Clean up client socket */
            if (server->client_sock >= 0) {
                zsock_close(server->client_sock);
                server->client_sock = -1;
                server->client_transport.sock = -1;
            }
        }
    }

    /* Thread terminating: close sockets owned by this thread */
    if (server->client_sock >= 0) {
        zsock_close(server->client_sock);
        server->client_sock = -1;
        server->client_transport.sock = -1;
    }

    if (server->listen_sock >= 0) {
        zsock_close(server->listen_sock);
        server->listen_sock = -1;
    }

    atomic_set(&server->thread_active, 0);
    k_sem_give(&server->stop_sem);
}

int nmbs_z_server_tcp_init(nmbs_z_server_tcp_t *server,
                           const nmbs_callbacks *callbacks,
                           uint16_t port)
{
    if (!server || !callbacks || port == 0) {
        return -EINVAL;
    }

    memset(server, 0, sizeof(*server));
    server->port = port;
    server->callbacks = *callbacks;
    server->callbacks.initialized = 0xFFFFDEBE;
    server->listen_sock = -1;
    server->client_sock = -1;
    server->client_transport.sock = -1;
    atomic_set(&server->running, 0);
    atomic_set(&server->thread_active, 0);
    k_sem_init(&server->stop_sem, 0, 1);
    k_sem_init(&server->ready_sem, 0, 1);
    server->priority = CONFIG_NANOMODBUS_SERVER_TCP_PRIORITY;

    nmbs_platform_conf platform_conf;
    nmbs_platform_conf_create(&platform_conf);
    platform_conf.transport = NMBS_TRANSPORT_TCP;
    platform_conf.read = nmbs_z_transport_tcp_read;
    platform_conf.write = nmbs_z_transport_tcp_write;
    platform_conf.flush = nmbs_z_transport_tcp_flush;
    platform_conf.arg = &server->client_transport;

    nmbs_error err = nmbs_server_create(&server->core, 0, &platform_conf, &server->callbacks);
    if (err != NMBS_ERROR_NONE) {
        return nmbs_z_error_to_errno(err);
    }

    nmbs_set_read_timeout(&server->core, CONFIG_NANOMODBUS_SERVER_TCP_TIMEOUT_MS);
    nmbs_set_byte_timeout(&server->core, CONFIG_NANOMODBUS_SERVER_TCP_TIMEOUT_MS);

    return 0;
}

int nmbs_z_server_tcp_start(nmbs_z_server_tcp_t *server,
                            const nmbs_callbacks *callbacks,
                            uint16_t port)
{
    if (!server) {
        return -EINVAL;
    }

    if (atomic_get(&server->running) || atomic_get(&server->thread_active)) {
        return -EALREADY;
    }

    if (callbacks != NULL || port != 0) {
        int ret = nmbs_z_server_tcp_init(server, callbacks, port);
        if (ret != 0) {
            return ret;
        }
    } else if (server->port == 0) {
        return -EINVAL;
    }

    if (!atomic_cas(&server->running, 0, 1)) {
        return -EALREADY;
    }

    k_sem_reset(&server->stop_sem);
    k_sem_reset(&server->ready_sem);
    atomic_set(&server->thread_active, 1);

    k_thread_create(&server->thread,
                    server->thread_stack,
                    K_KERNEL_STACK_SIZEOF(server->thread_stack),
                    tcp_server_thread_fn,
                    server, NULL, NULL,
                    server->priority,
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&server->thread, "nmbs_server_tcp");

    int ret = k_sem_take(&server->ready_sem, K_MSEC(2000));
    if (ret != 0 || !atomic_get(&server->running)) {
        atomic_set(&server->running, 0);
        /* The thread always gives stop_sem when it exits, including the
         * bind/listen failure paths.
         */
        (void)k_sem_take(&server->stop_sem, K_MSEC(2000));
        (void)k_thread_join(&server->thread, K_MSEC(1000));
        atomic_set(&server->thread_active, 0);
        return (ret != 0) ? -ETIMEDOUT : -EIO;
    }

    return 0;
}

int nmbs_z_server_tcp_stop(nmbs_z_server_tcp_t *server)
{
    if (!server) {
        return -EINVAL;
    }

    if (!atomic_get(&server->running) && !atomic_get(&server->thread_active)) {
        return 0;
    }

    atomic_set(&server->running, 0);

    /* Wake up blocking poll/recv calls. The server thread remains the only
     * owner that closes the descriptors.
     */
    if (server->client_sock >= 0) {
        (void)zsock_shutdown(server->client_sock, SHUT_RDWR);
    }

    if (server->listen_sock >= 0) {
        (void)zsock_shutdown(server->listen_sock, SHUT_RDWR);
    }

    int ret = k_sem_take(&server->stop_sem, K_MSEC(2000));
    if (ret != 0) {
        LOG_WRN("TCP server thread did not stop in time, forcing termination");
        k_thread_abort(&server->thread);
        k_sem_give(&server->stop_sem);
    }

    (void)k_thread_join(&server->thread, K_MSEC(1000));
    atomic_set(&server->thread_active, 0);

    return 0;
}

#endif /* CONFIG_NANOMODBUS_SERVER_TCP */
