/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(nmbs_tcp, CONFIG_NANOMODBUS_LOG_LEVEL);

/*
 * Client-side socket options: disable Nagle (Modbus exchanges are small and
 * latency sensitive) and optionally enable keepalive so a vanished peer is
 * detected instead of blocking the client forever.
 */
static void tcp_apply_socket_options(int sock)
{
    int opt = 1;
    int ret = zsock_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    if (ret < 0) {
        LOG_WRN("TCP_NODELAY setsockopt failed: %d", errno);
    }

#if defined(CONFIG_NANOMODBUS_TCP_KEEPALIVE)
    ret = zsock_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    if (ret < 0) {
        LOG_WRN("SO_KEEPALIVE setsockopt failed: %d", errno);
        return;
    }

    int idle = CONFIG_NANOMODBUS_TCP_KEEPIDLE_S;
    int intvl = CONFIG_NANOMODBUS_TCP_KEEPINTVL_S;
    int cnt = CONFIG_NANOMODBUS_TCP_KEEPCNT;

    if (zsock_setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0 ||
        zsock_setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)) < 0 ||
        zsock_setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) < 0) {
        LOG_WRN("TCP keepalive tuning failed: %d", errno);
    }
#endif /* CONFIG_NANOMODBUS_TCP_KEEPALIVE */
}

int nmbs_z_tcp_transport_connect(nmbs_z_tcp_transport_t *transport,
                                 const char *server_ip,
                                 uint16_t port,
                                 int32_t timeout_ms,
                                 nmbs_platform_conf *platform_conf)
{
    if (!transport || !server_ip || !platform_conf || port == 0) {
        return -EINVAL;
    }

    memset(transport, 0, sizeof(*transport));
    transport->sock = -1;
    transport->timeout_ms = timeout_ms;

    struct sockaddr_in *addr4 = (struct sockaddr_in *)&transport->server_addr;
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(port);
    int ret = zsock_inet_pton(AF_INET, server_ip, &addr4->sin_addr);
    if (ret == 1) {
        transport->addr_len = sizeof(struct sockaddr_in);
    } else {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&transport->server_addr;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        ret = zsock_inet_pton(AF_INET6, server_ip, &addr6->sin6_addr);
        if (ret != 1) {
            LOG_ERR("Invalid IP address: %s", server_ip);
            return -EINVAL;
        }
        transport->addr_len = sizeof(struct sockaddr_in6);
    }

    int sock = zsock_socket(transport->server_addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }
    transport->sock = sock;

    if (timeout_ms > 0) {
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    tcp_apply_socket_options(sock);

    ret = zsock_connect(sock, (struct sockaddr *)&transport->server_addr, transport->addr_len);
    if (ret < 0) {
        int err = -errno;
        LOG_ERR("Socket connect failed: %d", errno);
        zsock_close(sock);
        transport->sock = -1;
        return err;
    }

    nmbs_platform_conf_create(platform_conf);
    platform_conf->transport = NMBS_TRANSPORT_TCP;
    platform_conf->read = nmbs_z_transport_tcp_read;
    platform_conf->write = nmbs_z_transport_tcp_write;
    platform_conf->flush = nmbs_z_transport_tcp_flush;
    platform_conf->arg = transport;

    return 0;
}

/*
 * Wait until `events` is reported on the socket, honouring the byte timeout.
 *
 * Returns 1 when the socket is ready, 0 on timeout and a negative errno on
 * error.
 */
static int tcp_wait_for(int sock, short events, int32_t timeout_ms)
{
    struct zsock_pollfd pfd = {
        .fd = sock,
        .events = events,
    };

    int pret = zsock_poll(&pfd, 1, (timeout_ms < 0) ? -1 : timeout_ms);

    if (pret < 0) {
        return (errno == EINTR) ? -EINTR : -errno;
    }

    if (pret == 0) {
        return 0;
    }

    if (pfd.revents & ZSOCK_POLLNVAL) {
        return -EBADF;
    }

    if (!(pfd.revents & events)) {
        if (pfd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP)) {
            return -ECONNRESET;
        }
        return -EIO;
    }

    return 1;
}

int32_t nmbs_z_transport_tcp_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    if (!arg || !buf) {
        return -EINVAL;
    }

    if (count == 0) {
        return 0;
    }

    nmbs_z_tcp_transport_t *transport = (nmbs_z_tcp_transport_t *)arg;
    if (transport->sock < 0) {
        return -ENOTCONN;
    }

    uint16_t recvd = 0;

    while (recvd < count) {
        /* The byte timeout applies to the gap between two bytes, so the
         * deadline restarts every time data arrives.
         */
        if (byte_timeout_ms >= 0) {
            int ret = tcp_wait_for(transport->sock, ZSOCK_POLLIN, byte_timeout_ms);

            if (ret == -EINTR) {
                continue;
            }
            if (ret < 0) {
                return ret;
            }
            if (ret == 0) {
                break; /* Inter-byte timeout */
            }
        }

        ssize_t ret = zsock_recv(transport->sock, buf + recvd, count - recvd, MSG_DONTWAIT);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                if (byte_timeout_ms == 0) {
                    break;
                }
                continue;
            }
            return -errno;
        }

        if (ret == 0) {
            /* Peer closed the connection. A clean close after a complete
             * message is not an error; a close mid-message is.
             */
            if (recvd == 0) {
                return -ECONNRESET;
            }
            break;
        }

        recvd += (uint16_t)ret;
    }

    return (int32_t)recvd;
}

int32_t nmbs_z_transport_tcp_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    if (!arg || !buf) {
        return -EINVAL;
    }

    if (count == 0) {
        return 0;
    }

    nmbs_z_tcp_transport_t *transport = (nmbs_z_tcp_transport_t *)arg;
    if (transport->sock < 0) {
        return -ENOTCONN;
    }

    uint16_t sent = 0;

    while (sent < count) {
        if (byte_timeout_ms >= 0) {
            int ret = tcp_wait_for(transport->sock, ZSOCK_POLLOUT, byte_timeout_ms);

            if (ret == -EINTR) {
                continue;
            }
            if (ret < 0) {
                return ret;
            }
            if (ret == 0) {
                break; /* Socket stayed full for longer than the timeout */
            }
        }

        ssize_t ret = zsock_send(transport->sock, buf + sent, count - sent, MSG_DONTWAIT);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                if (byte_timeout_ms == 0) {
                    break;
                }
                continue;
            }
            return -errno;
        }

        if (ret == 0) {
            return -ECONNRESET;
        }

        sent += (uint16_t)ret;
    }

    return (int32_t)sent;
}

void nmbs_z_transport_tcp_flush(nmbs_t *nmbs, void *arg)
{
    ARG_UNUSED(nmbs);

    if (!arg) {
        return;
    }

    nmbs_z_tcp_transport_t *transport = (nmbs_z_tcp_transport_t *)arg;
    if (transport->sock < 0) {
        return;
    }

    uint8_t dummy[128];
    while (1) {
        ssize_t ret = zsock_recv(transport->sock, dummy, sizeof(dummy), MSG_DONTWAIT);
        if (ret <= 0) {
            break;
        }
    }
}

int nmbs_z_tcp_transport_close(nmbs_z_tcp_transport_t *transport)
{
    if (!transport) {
        return -EINVAL;
    }

    if (transport->sock >= 0) {
        zsock_close(transport->sock);
        transport->sock = -1;
    }

    return 0;
}
