/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

/**
 * @file nanomodbus_z.h
 * @brief Public interfaces and common definitions for nanoMODBUS Zephyr Module.
 */

#ifndef ZEPHYR_INCLUDE_NANOMODBUS_Z_H_
#define ZEPHYR_INCLUDE_NANOMODBUS_Z_H_

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <zephyr/kernel.h>
#ifndef CONFIG_NANOMODBUS_SERVER
#ifndef NMBS_SERVER_DISABLED
#define NMBS_SERVER_DISABLED
#endif
#endif

#ifndef CONFIG_NANOMODBUS_CLIENT
#ifndef NMBS_CLIENT_DISABLED
#define NMBS_CLIENT_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_COILS_DISABLED
#ifndef NMBS_SERVER_READ_COILS_DISABLED
#define NMBS_SERVER_READ_COILS_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_COILS_DISABLED
#define NMBS_CLIENT_READ_COILS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_DISCRETE_INPUTS_DISABLED
#ifndef NMBS_SERVER_READ_DISCRETE_INPUTS_DISABLED
#define NMBS_SERVER_READ_DISCRETE_INPUTS_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_DISCRETE_INPUTS_DISABLED
#define NMBS_CLIENT_READ_DISCRETE_INPUTS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_HOLDING_REGISTERS_DISABLED
#ifndef NMBS_SERVER_READ_HOLDING_REGISTERS_DISABLED
#define NMBS_SERVER_READ_HOLDING_REGISTERS_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_HOLDING_REGISTERS_DISABLED
#define NMBS_CLIENT_READ_HOLDING_REGISTERS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_INPUT_REGISTERS_DISABLED
#ifndef NMBS_SERVER_READ_INPUT_REGISTERS_DISABLED
#define NMBS_SERVER_READ_INPUT_REGISTERS_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_INPUT_REGISTERS_DISABLED
#define NMBS_CLIENT_READ_INPUT_REGISTERS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_WRITE_SINGLE_COIL_DISABLED
#ifndef NMBS_SERVER_WRITE_SINGLE_COIL_DISABLED
#define NMBS_SERVER_WRITE_SINGLE_COIL_DISABLED
#endif
#ifndef NMBS_CLIENT_WRITE_SINGLE_COIL_DISABLED
#define NMBS_CLIENT_WRITE_SINGLE_COIL_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_WRITE_SINGLE_REGISTER_DISABLED
#ifndef NMBS_SERVER_WRITE_SINGLE_REGISTER_DISABLED
#define NMBS_SERVER_WRITE_SINGLE_REGISTER_DISABLED
#endif
#ifndef NMBS_CLIENT_WRITE_SINGLE_REGISTER_DISABLED
#define NMBS_CLIENT_WRITE_SINGLE_REGISTER_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_WRITE_MULTIPLE_COILS_DISABLED
#ifndef NMBS_SERVER_WRITE_MULTIPLE_COILS_DISABLED
#define NMBS_SERVER_WRITE_MULTIPLE_COILS_DISABLED
#endif
#ifndef NMBS_CLIENT_WRITE_MULTIPLE_COILS_DISABLED
#define NMBS_CLIENT_WRITE_MULTIPLE_COILS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_WRITE_MULTIPLE_REGISTERS_DISABLED
#ifndef NMBS_SERVER_WRITE_MULTIPLE_REGISTERS_DISABLED
#define NMBS_SERVER_WRITE_MULTIPLE_REGISTERS_DISABLED
#endif
#ifndef NMBS_CLIENT_WRITE_MULTIPLE_REGISTERS_DISABLED
#define NMBS_CLIENT_WRITE_MULTIPLE_REGISTERS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_FILE_RECORD_DISABLED
#ifndef NMBS_SERVER_READ_FILE_RECORD_DISABLED
#define NMBS_SERVER_READ_FILE_RECORD_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_FILE_RECORD_DISABLED
#define NMBS_CLIENT_READ_FILE_RECORD_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_WRITE_FILE_RECORD_DISABLED
#ifndef NMBS_SERVER_WRITE_FILE_RECORD_DISABLED
#define NMBS_SERVER_WRITE_FILE_RECORD_DISABLED
#endif
#ifndef NMBS_CLIENT_WRITE_FILE_RECORD_DISABLED
#define NMBS_CLIENT_WRITE_FILE_RECORD_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_WRITE_REGISTERS_DISABLED
#ifndef NMBS_SERVER_READ_WRITE_REGISTERS_DISABLED
#define NMBS_SERVER_READ_WRITE_REGISTERS_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_WRITE_REGISTERS_DISABLED
#define NMBS_CLIENT_READ_WRITE_REGISTERS_DISABLED
#endif
#endif

#ifdef CONFIG_NANOMODBUS_FC_READ_DEVICE_IDENTIFICATION_DISABLED
#ifndef NMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED
#define NMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED
#endif
#ifndef NMBS_CLIENT_READ_DEVICE_IDENTIFICATION_DISABLED
#define NMBS_CLIENT_READ_DEVICE_IDENTIFICATION_DISABLED
#endif
#endif

#if defined(CONFIG_NANOMODBUS_BITFIELD_MAX) && !defined(NMBS_BITFIELD_MAX)
#define NMBS_BITFIELD_MAX CONFIG_NANOMODBUS_BITFIELD_MAX
#endif

#if defined(CONFIG_NANOMODBUS_DEBUG) && !defined(NMBS_DEBUG)
#define NMBS_DEBUG
#endif

#include <nanomodbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Converts a nanoMODBUS nmbs_error enum to a negative POSIX errno.
 *
 * @param err The nmbs_error code from nanoMODBUS core.
 * @return Negative integer errno (e.g. -ETIMEDOUT, -EINVAL, 0 for success).
 */
int nmbs_z_error_to_errno(nmbs_error err);

/**
 * @brief Returns a human-readable string representation of an nmbs_error.
 *
 * @param err The nmbs_error code.
 * @return Constant string description.
 */
const char *nmbs_z_strerror(nmbs_error err);

#ifdef CONFIG_NANOMODBUS_CLIENT

/**
 * @brief Thread-safe nanoMODBUS Zephyr Client instance.
 */
typedef struct nmbs_z_client {
    struct k_mutex lock;             /**< Mutex protecting core against concurrent callers */
    nmbs_t core;                     /**< Core nanoMODBUS client instance */
    nmbs_platform_conf platform;     /**< Platform transport configuration */
} nmbs_z_client_t;

/**
 * @brief Initialize a thread-safe nanoMODBUS client instance.
 *
 * @param client Pointer to uninitialized nmbs_z_client structure.
 * @param transport Platform transport configuration.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_init(nmbs_z_client_t *client, const nmbs_platform_conf *transport);

/**
 * @brief Set the target server RTU address for subsequent client requests.
 *
 * @param client Pointer to client instance.
 * @param unit_id Modbus RTU slave address (1-247).
 * @param timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on lock timeout.
 */
int nmbs_z_client_set_target_address(nmbs_z_client_t *client, uint8_t unit_id, k_timeout_t timeout);

/**
 * @brief Thread-safe Read Holding Registers (FC 03).
 */
int nmbs_z_client_read_holding_registers(nmbs_z_client_t *client, uint16_t address,
                                        uint16_t quantity, uint16_t *registers_out,
                                        k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Input Registers (FC 04).
 */
int nmbs_z_client_read_input_registers(nmbs_z_client_t *client, uint16_t address,
                                      uint16_t quantity, uint16_t *registers_out,
                                      k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Coils (FC 01).
 */
int nmbs_z_client_read_coils(nmbs_z_client_t *client, uint16_t address,
                             uint16_t quantity, nmbs_bitfield coils_out,
                             k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Discrete Inputs (FC 02).
 */
int nmbs_z_client_read_discrete_inputs(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, nmbs_bitfield inputs_out,
                                       k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Single Coil (FC 05).
 */
int nmbs_z_client_write_single_coil(nmbs_z_client_t *client, uint16_t address,
                                    bool value, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Single Register (FC 06).
 */
int nmbs_z_client_write_single_register(nmbs_z_client_t *client, uint16_t address,
                                        uint16_t value, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Multiple Coils (FC 15).
 */
int nmbs_z_client_write_multiple_coils(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, const nmbs_bitfield coils,
                                       k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Multiple Registers (FC 16).
 */
int nmbs_z_client_write_multiple_registers(nmbs_z_client_t *client, uint16_t address,
                                           uint16_t quantity, const uint16_t *registers,
                                           k_timeout_t lock_timeout);

#endif /* CONFIG_NANOMODBUS_CLIENT */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_H_ */
