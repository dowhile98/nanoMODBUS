/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nanomodbus_z_serial.h
 * @brief Modbus Serial RTU transport adapter and RS-485 DE control interface.
 */

#ifndef ZEPHYR_INCLUDE_NANOMODBUS_Z_SERIAL_H_
#define ZEPHYR_INCLUDE_NANOMODBUS_Z_SERIAL_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <nanomodbus.h>
#include "nanomodbus_z.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_NANOMODBUS_SERVER_RTU_RX_BUF_SIZE
#define CONFIG_NANOMODBUS_SERVER_RTU_RX_BUF_SIZE 260
#endif

/**
 * @brief Configuration for Modbus Serial RTU transport.
 */
typedef struct nmbs_z_serial_transport_config {
    const struct device *uart_dev;       /**< UART device handle */
    struct gpio_dt_spec de_gpio;         /**< RS-485 DE/RE GPIO pin spec (optional) */
    uint32_t baud_rate;                  /**< Baud rate in bps */
    uint32_t de_pre_delay_us;            /**< Guard time before TX with DE high (us) */
    uint32_t de_post_delay_us;           /**< Guard time after TX before DE low (us) */
} nmbs_z_serial_transport_config_t;

/**
 * @brief Serial RTU transport context instance.
 */
typedef struct nmbs_z_serial_transport {
    nmbs_z_serial_transport_config_t cfg;
    struct ring_buf rx_rb;
    uint8_t rx_buffer[CONFIG_NANOMODBUS_SERVER_RTU_RX_BUF_SIZE];
    uint8_t *rx_buf;
    uint32_t rx_buf_size;
    uint32_t t1_5_us;                    /**< 1.5 char silence interval in us */
    uint32_t t3_5_us;                    /**< 3.5 char silence interval in us */
    struct k_sem rx_sem;                 /**< Semaphore signaling received bytes */
} nmbs_z_serial_transport_t;

/**
 * @brief Calculate Modbus RTU character silence intervals (t1.5 and t3.5) in microseconds.
 *
 * For baud rates <= 19200:
 *   t1.5 = 16500000 / baud_rate
 *   t3.5 = 38500000 / baud_rate
 * For baud rates > 19200:
 *   t1.5 = 750 us
 *   t3.5 = 1750 us
 *
 * @param baud_rate Baud rate in bits per second.
 * @param t1_5_us Out-parameter for t1.5 interval in microseconds.
 * @param t3_5_us Out-parameter for t3.5 interval in microseconds.
 */
void nmbs_z_serial_calc_timings(uint32_t baud_rate, uint32_t *t1_5_us, uint32_t *t3_5_us);

/**
 * @brief Initialize a Serial RTU transport instance.
 *
 * Configures DE GPIO (if specified), initializes ring buffer and sync primitives,
 * calculates silence timing intervals, and populates the platform configuration.
 *
 * @param transport Pointer to transport context.
 * @param config Transport hardware and timing configuration.
 * @param platform_conf Out-parameter populated with nanoMODBUS platform callbacks.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_serial_transport_init(nmbs_z_serial_transport_t *transport,
                                 const nmbs_z_serial_transport_config_t *config,
                                 nmbs_platform_conf *platform_conf);

/**
 * @brief Platform read callback for Serial RTU transport.
 */
int32_t nmbs_z_transport_serial_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/**
 * @brief Platform write callback for Serial RTU transport with RS-485 DE pin control.
 */
int32_t nmbs_z_transport_serial_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/**
 * @brief Platform flush callback for Serial RTU transport.
 */
void nmbs_z_transport_serial_flush(nmbs_t *nmbs, void *arg);

/**
 * @brief Feed received bytes into the Serial RTU transport ring buffer and signal waiting reader.
 *
 * @param transport Pointer to transport context.
 * @param data Byte buffer containing received data.
 * @param length Number of bytes to put.
 * @return Number of bytes actually written to ring buffer.
 */
int nmbs_z_serial_transport_rx_put(nmbs_z_serial_transport_t *transport, const uint8_t *data, uint32_t length);

#ifdef CONFIG_NANOMODBUS_SERVER_RTU

/**
 * @brief Modbus RTU Server context instance.
 */
typedef struct nmbs_z_server_rtu {
    nmbs_t core;
    nmbs_callbacks callbacks;
    nmbs_z_serial_transport_t transport;
    struct k_timer silence_timer;
    struct k_work work;
    uint8_t server_address;
    bool running;
} nmbs_z_server_rtu_t;

/**
 * @brief Initialize and start an event-driven Modbus RTU Server.
 *
 * @param server Pointer to server instance.
 * @param config Serial configuration.
 * @param callbacks Modbus request handling callbacks.
 * @param server_address Modbus RTU slave address (1-247).
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_init(nmbs_z_server_rtu_t *server,
                           const nmbs_z_serial_transport_config_t *config,
                           const nmbs_callbacks *callbacks,
                           uint8_t server_address);

/**
 * @brief Start the Modbus RTU Server engine.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_start(nmbs_z_server_rtu_t *server);

/**
 * @brief Stop the Modbus RTU Server engine.
 *
 * Stops the silence timer and cancels any pending work on the workqueue.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_stop(nmbs_z_server_rtu_t *server);

/**
 * @brief Feed a received byte into the RTU Server engine and restart the silence timer.
 *
 * Can be called from UART ISR or polling thread.
 *
 * @param server Pointer to server instance.
 * @param byte Received byte.
 */
void nmbs_z_server_rtu_rx_byte(nmbs_z_server_rtu_t *server, uint8_t byte);

#endif /* CONFIG_NANOMODBUS_SERVER_RTU */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_SERIAL_H_ */
