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

#ifndef CONFIG_NANOMODBUS_SERIAL_RX_BUF_SIZE
#define CONFIG_NANOMODBUS_SERIAL_RX_BUF_SIZE 260
#endif

#ifndef CONFIG_NANOMODBUS_SERIAL_ASYNC_RX_BUF_SIZE
#define CONFIG_NANOMODBUS_SERIAL_ASYNC_RX_BUF_SIZE 128
#endif

/**
 * @brief How received bytes reach the serial transport.
 *
 * - @ref NMBS_Z_SERIAL_RX_EXTERNAL: the application calls
 *   nmbs_z_serial_transport_rx_put() (or nmbs_z_server_rtu_rx_byte()) itself,
 *   for example from a polling loop or a custom UART ISR.
 * - @ref NMBS_Z_SERIAL_RX_IRQ: the transport installs its own UART IRQ
 *   callback and drains the hardware FIFO in interrupt context.
 * - @ref NMBS_Z_SERIAL_RX_ASYNC: the transport uses the Zephyr asynchronous
 *   UART API (DMA or interrupt backed, depending on the driver) with two
 *   alternating receive buffers.
 *
 * IRQ and ASYNC modes require exclusive ownership of the UART instance.
 */
typedef enum nmbs_z_serial_rx_mode {
    NMBS_Z_SERIAL_RX_EXTERNAL = 0,
    NMBS_Z_SERIAL_RX_IRQ,
    NMBS_Z_SERIAL_RX_ASYNC,
} nmbs_z_serial_rx_mode_t;

/**
 * @brief RS-485 driver-enable (DE) control strategy.
 *
 * - @ref NMBS_Z_SERIAL_DE_AUTO: use hardware DE when the UART reports
 *   @ref UART_CFG_FLOW_CTRL_RS485, otherwise fall back to the GPIO in
 *   @ref nmbs_z_serial_transport_config::de_gpio when one is provided.
 * - @ref NMBS_Z_SERIAL_DE_HARDWARE: force hardware DE; the UART must be
 *   configured through devicetree (`de-enable`) and the transport will not
 *   touch a GPIO.
 * - @ref NMBS_Z_SERIAL_DE_GPIO: force software DE through
 *   @ref nmbs_z_serial_transport_config::de_gpio. This is the fallback for
 *   SoCs or UART drivers without hardware driver-enable support.
 * - @ref NMBS_Z_SERIAL_DE_NONE: no DE control (full-duplex link).
 */
typedef enum nmbs_z_serial_de_mode {
    NMBS_Z_SERIAL_DE_AUTO = 0,
    NMBS_Z_SERIAL_DE_HARDWARE,
    NMBS_Z_SERIAL_DE_GPIO,
    NMBS_Z_SERIAL_DE_NONE,
} nmbs_z_serial_de_mode_t;

struct nmbs_z_serial_transport;

/**
 * @brief Callback invoked whenever new bytes have been queued by the transport.
 *
 * Called from ISR context for IRQ and ASYNC receive modes, and from the caller
 * context in EXTERNAL mode. It is meant for lightweight work only, such as
 * restarting a frame-silence timer.
 */
typedef void (*nmbs_z_serial_rx_cb_t)(struct nmbs_z_serial_transport *transport, const uint8_t *data, size_t len,
                                      void *user_data);

/**
 * @brief Configuration for Modbus Serial RTU transport.
 */
typedef struct nmbs_z_serial_transport_config {
    const struct device *uart_dev;       /**< UART device handle */
    struct gpio_dt_spec de_gpio;         /**< RS-485 DE/RE GPIO pin spec (optional) */
    uint32_t baud_rate;                  /**< Baud rate in bps */
    uint32_t de_pre_delay_us;            /**< Guard time before TX with DE high (us, GPIO mode) */
    uint32_t de_post_delay_us;           /**< Guard time after TX before DE low (us, GPIO mode) */
    uint8_t parity;                      /**< enum uart_config_parity, 0 = none */
    uint8_t stop_bits;                   /**< enum uart_config_stop_bits, 0 = 1 stop bit */
    uint8_t data_bits;                   /**< enum uart_config_data_bits, 0 = 8 data bits */
    nmbs_z_serial_rx_mode_t rx_mode;     /**< Receive path selection */
    nmbs_z_serial_de_mode_t de_mode;     /**< Driver-enable control selection */
    nmbs_z_serial_rx_cb_t rx_cb;         /**< Optional receive notification callback */
    void *rx_cb_user_data;               /**< User data passed to @ref rx_cb */
} nmbs_z_serial_transport_config_t;

/**
 * @brief Serial RTU transport context instance.
 */
typedef struct nmbs_z_serial_transport {
    nmbs_z_serial_transport_config_t cfg;
    struct ring_buf rx_rb;
    uint8_t rx_buffer[CONFIG_NANOMODBUS_SERIAL_RX_BUF_SIZE];
    uint32_t t1_5_us;                    /**< 1.5 char silence interval in us */
    uint32_t t3_5_us;                    /**< 3.5 char silence interval in us */
    struct k_sem rx_sem;                 /**< Semaphore signaling received bytes */
    struct k_spinlock rb_lock;           /**< Protects ring buffer reset against RX ISR */
    atomic_t rx_overflow;                /**< Set when a frame did not fit in rx_buffer */
    atomic_t rx_active;                  /**< IRQ/ASYNC receiver currently enabled */
    atomic_t rx_resync_count;            /**< Frames discarded because of a > t1.5 gap */
    uint32_t last_rx_cycles;             /**< Cycle counter of the last queued byte */
    uint8_t async_rx_buffer[2][CONFIG_NANOMODBUS_SERIAL_ASYNC_RX_BUF_SIZE];
    uint8_t async_next_buf;              /**< Buffer index to hand to the driver next */
    bool de_hw;                          /**< UART hardware driver-enable is active */
    bool de_gpio;                        /**< Software GPIO driver-enable is active */
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
 * Applies the requested line settings (baud rate, parity, stop bits, data
 * bits) to the UART when the driver supports runtime configuration, keeping
 * the flow-control setting untouched so that a devicetree configured RS-485
 * hardware driver-enable keeps working. The DE strategy (hardware, GPIO or
 * none) is resolved afterwards, the ring buffer and synchronization
 * primitives are initialized and the silence timings are calculated. When
 * @ref nmbs_z_serial_transport_config::rx_mode is not EXTERNAL, reception is
 * started before this function returns.
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
 * @brief Start IRQ or ASYNC reception.
 *
 * Safe to call again after nmbs_z_serial_transport_stop_rx(). Does nothing for
 * EXTERNAL mode.
 *
 * @param transport Pointer to transport context.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_serial_transport_start_rx(nmbs_z_serial_transport_t *transport);

/**
 * @brief Stop IRQ or ASYNC reception.
 *
 * @param transport Pointer to transport context.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_serial_transport_stop_rx(nmbs_z_serial_transport_t *transport);

/**
 * @brief Stop reception, release the GPIO and drain pending data.
 *
 * @param transport Pointer to transport context.
 */
void nmbs_z_serial_transport_deinit(nmbs_z_serial_transport_t *transport);

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
 * May be called from ISR context. Invokes the configured receive notification
 * callback, if any.
 *
 * In IRQ and ASYNC receive modes a gap longer than t1.5 between two calls
 * discards the partially received frame, as required by the Modbus over
 * serial line specification. EXTERNAL mode is exempt: there the application
 * owns the byte timing.
 *
 * @param transport Pointer to transport context.
 * @param data Byte buffer containing received data.
 * @param length Number of bytes to put.
 * @return Number of bytes actually written to ring buffer.
 */
int nmbs_z_serial_transport_rx_put(nmbs_z_serial_transport_t *transport, const uint8_t *data, uint32_t length);

#ifdef CONFIG_NANOMODBUS_SERVER_RTU

#ifndef CONFIG_NANOMODBUS_SERVER_RTU_WQ_STACK_SIZE
#define CONFIG_NANOMODBUS_SERVER_RTU_WQ_STACK_SIZE 1024
#endif

#ifndef CONFIG_NANOMODBUS_SERVER_RTU_WQ_PRIORITY
#define CONFIG_NANOMODBUS_SERVER_RTU_WQ_PRIORITY 5
#endif

/**
 * @brief Modbus RTU Server context instance.
 *
 * Owns its workqueue thread stack, so an instance must be allocated in static
 * storage (or at least must outlive the server) and needs roughly
 * CONFIG_NANOMODBUS_SERVER_RTU_WQ_STACK_SIZE extra bytes.
 */
typedef struct nmbs_z_server_rtu {
    nmbs_t core;
    nmbs_callbacks callbacks;
    nmbs_z_serial_transport_t transport;
    struct k_timer silence_timer;
    struct k_work work;
    struct k_work_q work_q;              /**< Dedicated queue for request processing */
    K_KERNEL_STACK_MEMBER(work_q_stack, CONFIG_NANOMODBUS_SERVER_RTU_WQ_STACK_SIZE);
    uint8_t server_address;
    atomic_t running;                    /**< Server engine accepts requests */
    atomic_t rx_pending;                 /**< Bytes arrived during the last poll */
    atomic_t init_magic;                 /**< Guards against double initialization */
} nmbs_z_server_rtu_t;

/**
 * @brief Initialize and start an event-driven Modbus RTU Server.
 *
 * The receive mode is taken from @p config. In IRQ and ASYNC modes the server
 * installs its own receive notification callback, so the application must not
 * also register a UART callback on the same device.
 *
 * @p server must be zero-initialized static storage: it embeds a workqueue
 * thread stack and stays referenced by the kernel after this call returns.
 *
 * @param server Pointer to server instance.
 * @param config Serial configuration.
 * @param callbacks Modbus request handling callbacks.
 * @param server_address Modbus RTU slave address (1-247).
 * @return 0 on success, -EALREADY if the instance was already initialized,
 *         other negative errno on failure.
 */
int nmbs_z_server_rtu_init(nmbs_z_server_rtu_t *server,
                           const nmbs_z_serial_transport_config_t *config,
                           const nmbs_callbacks *callbacks,
                           uint8_t server_address);

/**
 * @brief Start the Modbus RTU Server engine.
 *
 * Re-enables reception after nmbs_z_server_rtu_stop(); the workqueue thread is
 * created once by nmbs_z_server_rtu_init() and is not restarted here.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_start(nmbs_z_server_rtu_t *server);

/**
 * @brief Stop the Modbus RTU Server engine.
 *
 * Stops the silence timer, cancels any pending work on the workqueue, disables
 * IRQ/ASYNC reception and discards a partially received request. The
 * workqueue thread keeps running so that nmbs_z_server_rtu_start() can be
 * called again.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_stop(nmbs_z_server_rtu_t *server);

/**
 * @brief Feed a received byte into the RTU Server engine and restart the silence timer.
 *
 * Only needed in @ref NMBS_Z_SERIAL_RX_EXTERNAL mode, or when injecting bytes
 * from a custom transport.
 *
 * @param server Pointer to server instance.
 * @param byte Received byte.
 */
void nmbs_z_server_rtu_rx_byte(nmbs_z_server_rtu_t *server, uint8_t byte);

/**
 * @brief Feed a received buffer into the RTU Server engine and restart the silence timer.
 *
 * @param server Pointer to server instance.
 * @param data Received bytes.
 * @param length Number of bytes.
 */
void nmbs_z_server_rtu_rx_data(nmbs_z_server_rtu_t *server, const uint8_t *data, uint32_t length);

#endif /* CONFIG_NANOMODBUS_SERVER_RTU */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_SERIAL_H_ */
