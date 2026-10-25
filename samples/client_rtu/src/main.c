/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Modbus RTU client sample.
 *
 * USART2 is the Modbus bus. On the Nucleo-H743ZI the ST-LINK virtual COM port
 * (and therefore the console) is USART3, so the two do not conflict. The
 * board overlay only muxes TX/RX: this sample assumes a full-duplex link, such
 * as the USB-RS232 converter used on the bench. Add a `modbus-de` GPIO (or
 * hardware `de-enable` in devicetree) if your transceiver needs DE control.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

LOG_MODULE_REGISTER(client_rtu_app, CONFIG_NANOMODBUS_LOG_LEVEL);

#define SERVER_ADDRESS 1
#define POLL_INTERVAL_MS 1000
#define READ_QUANTITY 4
#define HEARTBEAT_REGISTER 10

#if DT_HAS_ALIAS(modbus_uart)
#define MODBUS_UART_NODE DT_ALIAS(modbus_uart)
#elif DT_NODE_EXISTS(DT_CHOSEN(zephyr_console))
#define MODBUS_UART_NODE DT_CHOSEN(zephyr_console)
#elif DT_NODE_EXISTS(DT_NODELABEL(usart3))
#define MODBUS_UART_NODE DT_NODELABEL(usart3)
#else
#define MODBUS_UART_NODE DT_INVALID_NODE
#endif

static const char *rx_mode_name(nmbs_z_serial_rx_mode_t mode)
{
    switch (mode) {
    case NMBS_Z_SERIAL_RX_IRQ:
        return "IRQ";
    case NMBS_Z_SERIAL_RX_ASYNC:
        return "async/DMA";
    case NMBS_Z_SERIAL_RX_EXTERNAL:
    default:
        return "external polling";
    }
}

/*
 * Pick the best receive path the UART actually supports. Async RX needs both
 * the async API and DMA channels in devicetree, and the DMA-visible buffers
 * must be non-cacheable (the transport keeps them in .bss), so a
 * cache-enabled MCU falls back to IRQ mode here.
 */
static nmbs_z_serial_rx_mode_t select_rx_mode(void)
{
#if defined(CONFIG_UART_ASYNC_API) && DT_NODE_EXISTS(MODBUS_UART_NODE) && !defined(CONFIG_DCACHE)
    if (DT_NODE_HAS_PROP(MODBUS_UART_NODE, dmas)) {
        return NMBS_Z_SERIAL_RX_ASYNC;
    }
#endif
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
    return NMBS_Z_SERIAL_RX_IRQ;
#else
    return NMBS_Z_SERIAL_RX_EXTERNAL;
#endif
}

int main(void)
{
    LOG_INF("nanoMODBUS RTU Client Sample Application Starting...");

    const struct device *uart_dev = NULL;
#if DT_NODE_EXISTS(MODBUS_UART_NODE)
    uart_dev = DEVICE_DT_GET(MODBUS_UART_NODE);
#endif

    if (!uart_dev || !device_is_ready(uart_dev)) {
        LOG_ERR("Modbus UART not ready or not found");
        return -ENODEV;
    }

    /* The transport registration lasts as long as the client. */
    static nmbs_z_serial_transport_t transport;
    nmbs_z_serial_transport_config_t config = {
        .uart_dev = uart_dev,
        .baud_rate = 115200,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .de_mode = NMBS_Z_SERIAL_DE_AUTO,
        .rx_mode = select_rx_mode(),
    };
#if DT_HAS_ALIAS(modbus_de)
    config.de_gpio = (struct gpio_dt_spec)GPIO_DT_SPEC_GET(DT_ALIAS(modbus_de), gpios);
#endif

    nmbs_platform_conf platform;
    int ret = nmbs_z_serial_transport_init(&transport, &config, &platform);
    if (ret != 0) {
        LOG_ERR("Failed to initialize RTU transport: %d", ret);
        return ret;
    }

    static nmbs_z_client_t client;
    ret = nmbs_z_client_init(&client, &platform);
    if (ret != 0) {
        LOG_ERR("Failed to initialize Modbus client: %d", ret);
        return ret;
    }

    ret = nmbs_z_client_set_target_address(&client, SERVER_ADDRESS, K_MSEC(100));
    if (ret != 0) {
        LOG_ERR("Failed to set target address: %d", ret);
        return ret;
    }

    LOG_INF("Modbus RTU client started (server %u, 115200 bps, RX %s)", SERVER_ADDRESS,
            rx_mode_name(config.rx_mode));

    uint16_t heartbeat = 0;

    while (1) {
        uint16_t registers[READ_QUANTITY] = {0};

        ret = nmbs_z_client_read_holding_registers(&client, 0, READ_QUANTITY, registers, K_MSEC(500));
        if (ret == 0) {
            LOG_INF("Holding registers [0..%d]: %u, %u, %u, %u", READ_QUANTITY - 1, registers[0],
                    registers[1], registers[2], registers[3]);
        } else {
            LOG_WRN("Read failed: %d (nmbs_error %d)", ret, nmbs_z_client_get_last_error(&client));
        }

        ret = nmbs_z_client_write_single_register(&client, HEARTBEAT_REGISTER, heartbeat, K_MSEC(500));
        if (ret == 0) {
            LOG_INF("Heartbeat register [%u] <- %u", HEARTBEAT_REGISTER, heartbeat);
        } else {
            LOG_WRN("Write failed: %d (nmbs_error %d)", ret, nmbs_z_client_get_last_error(&client));
        }

        heartbeat++;
        k_sleep(K_MSEC(POLL_INTERVAL_MS));
    }

    return 0;
}
