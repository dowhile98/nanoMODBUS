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
 * @brief Converts a POSIX errno to a nanoMODBUS nmbs_error enum.
 *
 * @param err The POSIX errno (positive or negative).
 * @return nmbs_error code.
 */
nmbs_error nmbs_z_errno_to_error(int err);

/**
 * @brief Converts a POSIX errno to a nanoMODBUS nmbs_error enum (alias).
 *
 * @param err The POSIX errno (positive or negative).
 * @return nmbs_error code.
 */
nmbs_error errno_to_nmbs_error(int err);

/**
 * @brief Returns a human-readable string representation of an nmbs_error.
 *
 * @param err The nmbs_error code.
 * @return Constant string description.
 */
const char *nmbs_z_strerror(nmbs_error err);

/**
 * @brief Modbus server runtime diagnostics counters.
 */
struct nmbs_z_stats {
    uint32_t rx_frames;  /**< Total frames received */
    uint32_t tx_frames;  /**< Total frames transmitted */
    uint32_t crc_errors; /**< Total CRC errors detected */
    uint32_t exceptions; /**< Total Modbus exceptions generated/sent */
    uint32_t timeouts;   /**< Total frame receive timeouts */
};

typedef struct nmbs_z_server_rtu nmbs_z_server_rtu_t;
typedef struct nmbs_z_server_tcp nmbs_z_server_tcp_t;

/**
 * @brief Retrieve runtime diagnostic statistics from an RTU Server instance.
 *
 * @param server Pointer to server instance.
 * @param stats Pointer to output statistics structure.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_get_stats(const nmbs_z_server_rtu_t *server, struct nmbs_z_stats *stats);

/**
 * @brief Reset runtime diagnostic statistics on an RTU Server instance.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_rtu_reset_stats(nmbs_z_server_rtu_t *server);

/**
 * @brief Retrieve runtime diagnostic statistics from a TCP Server instance.
 *
 * @param server Pointer to server instance.
 * @param stats Pointer to output statistics structure.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_get_stats(const nmbs_z_server_tcp_t *server, struct nmbs_z_stats *stats);

/**
 * @brief Reset runtime diagnostic statistics on a TCP Server instance.
 *
 * @param server Pointer to server instance.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_server_tcp_reset_stats(nmbs_z_server_tcp_t *server);

#ifdef CONFIG_NANOMODBUS_CLIENT

/**
 * @brief Thread-safe nanoMODBUS Zephyr Client instance.
 */
typedef struct nmbs_z_client {
    struct k_mutex lock;             /**< Mutex protecting core against concurrent callers */
    nmbs_t core;                     /**< Core nanoMODBUS client instance */
    nmbs_platform_conf platform;     /**< Platform transport configuration */
    int32_t read_timeout_ms;         /**< First-byte/response timeout in milliseconds */
    int32_t byte_timeout_ms;         /**< Inter-byte timeout in milliseconds */
    nmbs_error last_error;           /**< Raw result of the last request, including exception codes */
} nmbs_z_client_t;

/**
 * @brief Initialize a thread-safe nanoMODBUS client instance.
 *
 * Read and byte timeouts are initialized from
 * CONFIG_NANOMODBUS_CLIENT_DEFAULT_READ_TIMEOUT_MS and
 * CONFIG_NANOMODBUS_CLIENT_DEFAULT_BYTE_TIMEOUT_MS.
 *
 * @param client Pointer to uninitialized nmbs_z_client structure.
 * @param transport Platform transport configuration.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_init(nmbs_z_client_t *client, const nmbs_platform_conf *transport);

/**
 * @brief Set the client response and inter-byte timeouts.
 *
 * @param client Pointer to client instance.
 * @param read_timeout_ms Time to wait for the first byte of a response.
 *                        Negative values mean infinite.
 * @param byte_timeout_ms Maximum gap between two response bytes.
 *                        Negative values mean infinite.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_set_timeouts(nmbs_z_client_t *client, int32_t read_timeout_ms, int32_t byte_timeout_ms);

/**
 * @brief Return the raw nmbs_error produced by the most recent client call.
 *
 * The errno returned by the request helpers cannot carry a Modbus exception
 * code: exceptions 1..4 are reported as ordinary negative errno values, so the
 * caller cannot tell which exception the server raised. This getter exposes the
 * unmodified nmbs_error, so callers can distinguish
 * - a Modbus exception (value 1..4, see nmbs_error_is_exception()),
 * - a transport problem (NMBS_ERROR_TRANSPORT, NMBS_ERROR_TIMEOUT, ...),
 * - a malformed response (NMBS_ERROR_INVALID_RESPONSE, NMBS_ERROR_CRC, ...).
 *
 * The value is updated by every client helper, including failures to acquire
 * the client lock (reported as NMBS_ERROR_TIMEOUT). It is a per-client, not
 * per-call, value: with several threads sharing one client, read it under the
 * same serialization the callers already use.
 *
 * @param client Pointer to client instance.
 * @return Raw nmbs_error of the last operation, NMBS_ERROR_INVALID_ARGUMENT if
 *         @p client is NULL.
 */
nmbs_error nmbs_z_client_get_last_error(const nmbs_z_client_t *client);

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

/**
 * @brief Thread-safe Read File Record (FC 20).
 *
 * @param client Pointer to client instance.
 * @param file_number File number (1-65535).
 * @param record_number Record number inside the file (0-9999).
 * @param registers_out Array receiving @p quantity registers.
 * @param quantity Number of registers to read.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure. Use
 *         nmbs_z_client_get_last_error() for the unmodified error code.
 */
int nmbs_z_client_read_file_record(nmbs_z_client_t *client, uint16_t file_number,
                                   uint16_t record_number, uint16_t *registers_out,
                                   uint16_t quantity, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write File Record (FC 21).
 *
 * @param client Pointer to client instance.
 * @param file_number File number (1-65535).
 * @param record_number Record number inside the file (0-9999).
 * @param registers Array of @p quantity registers to write.
 * @param quantity Number of registers to write.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_write_file_record(nmbs_z_client_t *client, uint16_t file_number,
                                    uint16_t record_number, const uint16_t *registers,
                                    uint16_t quantity, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read/Write Multiple Registers (FC 23).
 *
 * Performs the write and the read in a single Modbus transaction, so no other
 * thread can interleave a request between them.
 *
 * @param client Pointer to client instance.
 * @param read_address Starting address of the registers to read.
 * @param read_quantity Number of registers to read.
 * @param registers_out Array receiving @p read_quantity registers.
 * @param write_address Starting address of the registers to write.
 * @param write_quantity Number of registers to write.
 * @param registers Array of @p write_quantity registers to write.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_read_write_registers(nmbs_z_client_t *client, uint16_t read_address,
                                       uint16_t read_quantity, uint16_t *registers_out,
                                       uint16_t write_address, uint16_t write_quantity,
                                       const uint16_t *registers, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Holding Registers with explicit unit ID (FC 03).
 */
int nmbs_z_client_read_holding_registers_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                              uint16_t address, uint16_t quantity,
                                              uint16_t *registers_out, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Input Registers with explicit unit ID (FC 04).
 */
int nmbs_z_client_read_input_registers_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                            uint16_t address, uint16_t quantity,
                                            uint16_t *registers_out, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Coils with explicit unit ID (FC 01).
 */
int nmbs_z_client_read_coils_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                  uint16_t address, uint16_t quantity,
                                  nmbs_bitfield coils_out, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Discrete Inputs with explicit unit ID (FC 02).
 */
int nmbs_z_client_read_discrete_inputs_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                            uint16_t address, uint16_t quantity,
                                            nmbs_bitfield inputs_out, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Single Coil with explicit unit ID (FC 05).
 */
int nmbs_z_client_write_single_coil_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                        uint16_t address, bool value, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Single Register with explicit unit ID (FC 06).
 */
int nmbs_z_client_write_single_register_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                            uint16_t address, uint16_t value, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Multiple Coils with explicit unit ID (FC 15).
 */
int nmbs_z_client_write_multiple_coils_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                           uint16_t address, uint16_t quantity,
                                           const nmbs_bitfield coils, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Write Multiple Registers with explicit unit ID (FC 16).
 */
int nmbs_z_client_write_multiple_registers_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                               uint16_t address, uint16_t quantity,
                                               const uint16_t *registers, k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read/Write Multiple Registers with explicit unit ID (FC 23).
 */
int nmbs_z_client_read_write_registers_addr(nmbs_z_client_t *client, uint8_t unit_id,
                                           uint16_t read_address, uint16_t read_quantity,
                                           uint16_t *registers_out, uint16_t write_address,
                                           uint16_t write_quantity, const uint16_t *registers,
                                           k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Device Identification, basic objects (FC 43/14, code 1).
 *
 * @param client Pointer to client instance.
 * @param vendor_name Buffer receiving the VendorName object.
 * @param product_code Buffer receiving the ProductCode object.
 * @param major_minor_revision Buffer receiving the MajorMinorRevision object.
 * @param buffers_length Length of each of the three buffers.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_read_device_identification_basic(nmbs_z_client_t *client,
                                                   char *vendor_name, char *product_code,
                                                   char *major_minor_revision,
                                                   uint8_t buffers_length,
                                                   k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Device Identification, regular objects (FC 43/14, code 2).
 *
 * @param client Pointer to client instance.
 * @param vendor_url Buffer receiving the VendorUrl object.
 * @param product_name Buffer receiving the ProductName object.
 * @param model_name Buffer receiving the ModelName object.
 * @param user_application_name Buffer receiving the UserApplicationName object.
 * @param buffers_length Length of each of the four buffers.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_read_device_identification_regular(nmbs_z_client_t *client,
                                                     char *vendor_url, char *product_name,
                                                     char *model_name,
                                                     char *user_application_name,
                                                     uint8_t buffers_length,
                                                     k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Device Identification, extended objects (FC 43/14, code 3).
 *
 * @param client Pointer to client instance.
 * @param object_id_start First object id to request (must be >= 0x80).
 * @param ids Array receiving the object ids, @p ids_length entries.
 * @param buffers Array of @p ids_length buffers receiving the object values.
 * @param ids_length Number of entries in @p ids and @p buffers.
 * @param buffer_length Length of every buffer in @p buffers.
 * @param objects_count_out Number of objects actually returned.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_read_device_identification_extended(nmbs_z_client_t *client,
                                                      uint8_t object_id_start, uint8_t *ids,
                                                      char **buffers, uint8_t ids_length,
                                                      uint8_t buffer_length,
                                                      uint8_t *objects_count_out,
                                                      k_timeout_t lock_timeout);

/**
 * @brief Thread-safe Read Device Identification, single object (FC 43/14, code 4).
 *
 * @param client Pointer to client instance.
 * @param object_id Object id to read (0x00-0x06 or >= 0x80).
 * @param buffer Buffer receiving the object value.
 * @param buffer_length Length of @p buffer.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_read_device_identification(nmbs_z_client_t *client, uint8_t object_id,
                                             char *buffer, uint8_t buffer_length,
                                             k_timeout_t lock_timeout);

/**
 * @brief Thread-safe raw Modbus PDU exchange.
 *
 * Sends an arbitrary function code and receives exactly @p response_length
 * data bytes of the response. Both halves run under a single lock acquisition,
 * so no other thread can inject a request between them (which would happen if
 * nmbs_send_raw_pdu() and nmbs_receive_raw_pdu_response() were called
 * separately through two client helpers).
 *
 * @param client Pointer to client instance.
 * @param fc Function code to send.
 * @param data Request payload (may be NULL when @p data_length is 0).
 * @param data_length Request payload length.
 * @param response Buffer receiving the response payload (may be NULL to
 *                 discard it).
 * @param response_length Number of response payload bytes to consume.
 * @param lock_timeout Lock acquisition timeout.
 * @return 0 on success, negative errno on failure.
 */
int nmbs_z_client_raw_pdu(nmbs_z_client_t *client, uint8_t fc, const uint8_t *data,
                          uint16_t data_length, uint8_t *response,
                          uint8_t response_length, k_timeout_t lock_timeout);

#endif /* CONFIG_NANOMODBUS_CLIENT */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NANOMODBUS_Z_H_ */
