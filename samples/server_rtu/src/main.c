/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

LOG_MODULE_REGISTER(server_rtu_app, CONFIG_NANOMODBUS_LOG_LEVEL);

#define HOLDING_REG_COUNT 16
#define COIL_COUNT 16

static uint16_t holding_registers[HOLDING_REG_COUNT];
static uint8_t coils[(COIL_COUNT + 7) / 8];

static nmbs_error handle_read_holding_registers(uint16_t address, uint16_t quantity,
                                                uint16_t *registers_out, uint8_t unit_id,
                                                void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Read holding registers: addr=%u, qty=%u, unit_id=%u", address, quantity, unit_id);
    if (address + quantity > HOLDING_REG_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = holding_registers[address + i];
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_write_single_register(uint16_t address, uint16_t value,
                                               uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Write single register: addr=%u, val=0x%04X, unit_id=%u", address, value, unit_id);
    if (address >= HOLDING_REG_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    holding_registers[address] = value;
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_write_multiple_registers(uint16_t address, uint16_t quantity,
                                                  const uint16_t *registers, uint8_t unit_id,
                                                  void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Write multiple registers: addr=%u, qty=%u, unit_id=%u", address, quantity, unit_id);
    if (address + quantity > HOLDING_REG_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        holding_registers[address + i] = registers[i];
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_read_coils(uint16_t address, uint16_t quantity,
                                    nmbs_bitfield coils_out, uint8_t unit_id,
                                    void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Read coils: addr=%u, qty=%u, unit_id=%u", address, quantity, unit_id);
    if (address + quantity > COIL_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t idx = address + i;
        bool state = (coils[idx / 8] & (1 << (idx % 8))) != 0;
        nmbs_bitfield_write(coils_out, i, state);
    }
    return NMBS_ERROR_NONE;
}

static nmbs_error handle_write_single_coil(uint16_t address, bool value,
                                           uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    LOG_INF("Write single coil: addr=%u, val=%d, unit_id=%u", address, value, unit_id);
    if (address >= COIL_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    if (value) {
        coils[address / 8] |= (1 << (address % 8));
    } else {
        coils[address / 8] &= ~(1 << (address % 8));
    }
    return NMBS_ERROR_NONE;
}

#if DT_NODE_EXISTS(DT_ALIAS(modbus_uart))
#define MODBUS_UART_NODE DT_ALIAS(modbus_uart)
#elif DT_NODE_EXISTS(DT_CHOSEN(zephyr_console))
#define MODBUS_UART_NODE DT_CHOSEN(zephyr_console)
#elif DT_NODE_EXISTS(DT_NODELABEL(usart3))
#define MODBUS_UART_NODE DT_NODELABEL(usart3)
#else
#define MODBUS_UART_NODE DT_INVALID_NODE
#endif

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static void uart_irq_cb(const struct device *dev, void *user_data)
{
    nmbs_z_server_rtu_t *server = (nmbs_z_server_rtu_t *)user_data;
    uart_irq_update(dev);
    while (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        if (uart_fifo_read(dev, &byte, 1) <= 0) {
            break;
        }
        nmbs_z_server_rtu_rx_byte(server, byte);
    }
}
#endif

int main(void)
{
    LOG_INF("nanoMODBUS RTU Server Sample Application Starting...");

    for (int i = 0; i < HOLDING_REG_COUNT; i++) {
        holding_registers[i] = (uint16_t)(0x1000 + i);
    }

    const struct device *uart_dev = NULL;
#if DT_NODE_EXISTS(MODBUS_UART_NODE)
    uart_dev = DEVICE_DT_GET(MODBUS_UART_NODE);
#endif

    if (!uart_dev || !device_is_ready(uart_dev)) {
        LOG_WRN("UART device not ready or not found; running in simulated idle loop");
    }

    nmbs_z_serial_transport_config_t config = {
        .uart_dev = uart_dev,
        .baud_rate = 115200,
        .de_pre_delay_us = 0,
        .de_post_delay_us = 0,
    };
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(modbus_de))
    config.de_gpio = (struct gpio_dt_spec)GPIO_DT_SPEC_GET(DT_ALIAS(modbus_de), gpios);
#endif

    nmbs_callbacks cbs = {
        .read_holding_registers = handle_read_holding_registers,
        .write_single_register = handle_write_single_register,
        .write_multiple_registers = handle_write_multiple_registers,
        .read_coils = handle_read_coils,
        .write_single_coil = handle_write_single_coil,
    };

    static nmbs_z_server_rtu_t server;
    int ret = nmbs_z_server_rtu_init(&server, &config, &cbs, 1 /* RTU slave address */);
    if (ret != 0) {
        LOG_ERR("Failed to initialize RTU Server: %d", ret);
        return ret;
    }

    ret = nmbs_z_server_rtu_start(&server);
    if (ret != 0) {
        LOG_ERR("Failed to start RTU Server: %d", ret);
        return ret;
    }

    LOG_INF("Modbus RTU Server started successfully (Slave ID 1, 115200 bps)");

    if (uart_dev && device_is_ready(uart_dev)) {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
        if (uart_irq_callback_user_data_set(uart_dev, uart_irq_cb, &server) == 0) {
            uart_irq_rx_enable(uart_dev);
            LOG_INF("Interrupt-driven UART reception enabled");
            while (1) {
                k_sleep(K_SECONDS(1));
            }
        }
#endif
        LOG_INF("Polling UART for incoming Modbus frames...");
        while (1) {
            unsigned char c;
            if (uart_poll_in(uart_dev, &c) == 0) {
                nmbs_z_server_rtu_rx_byte(&server, (uint8_t)c);
            } else {
                k_sleep(K_MSEC(1));
            }
        }
    } else {
        while (1) {
            k_sleep(K_SECONDS(1));
        }
    }

    return 0;
}
