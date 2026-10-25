/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#ifdef CONFIG_NANOMODBUS_CLIENT

LOG_MODULE_REGISTER(nmbs_client, CONFIG_NANOMODBUS_LOG_LEVEL);

static inline int client_lock(nmbs_z_client_t *client, k_timeout_t lock_timeout)
{
    int ret = k_mutex_lock(&client->lock, lock_timeout);
    if (ret != 0) {
        /* K_NO_WAIT reports -EBUSY, a timed-out wait reports -EAGAIN. Both are
         * lock contention from the caller's point of view.
         */
        if (ret == -EAGAIN || ret == -EBUSY) {
            LOG_DBG("Client lock contention: timeout waiting for mutex");
            client->last_error = NMBS_ERROR_TIMEOUT;
            return -ETIMEDOUT;
        }
        LOG_DBG("Client lock contention: %d", ret);
        client->last_error = NMBS_ERROR_INVALID_ARGUMENT;
        return ret;
    }
    LOG_DBG("Client lock acquired");
    return 0;
}

/*
 * Record the unmodified nmbs_error before returning its errno translation, so
 * a Modbus exception code (1..4) is not lost in the errno mapping. Called with
 * client->lock held.
 */
static inline void client_record_error(nmbs_z_client_t *client, nmbs_error err)
{
    client->last_error = err;
}

nmbs_error nmbs_z_client_get_last_error(const nmbs_z_client_t *client)
{
    if (client == NULL) {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    return client->last_error;
}

int nmbs_z_client_init(nmbs_z_client_t *client, const nmbs_platform_conf *transport)
{
    if (client == NULL || transport == NULL) {
        LOG_ERR("Client init failed: NULL parameter");
        return -EINVAL;
    }

    k_mutex_init(&client->lock);
    client->platform = *transport;
    client->read_timeout_ms = CONFIG_NANOMODBUS_CLIENT_DEFAULT_READ_TIMEOUT_MS;
    client->byte_timeout_ms = CONFIG_NANOMODBUS_CLIENT_DEFAULT_BYTE_TIMEOUT_MS;
    client->last_error = NMBS_ERROR_NONE;

    nmbs_error err = nmbs_client_create(&client->core, &client->platform);
    if (err != NMBS_ERROR_NONE) {
        client->last_error = err;
        LOG_ERR("Client create failed: %d", err);
    } else {
        nmbs_set_read_timeout(&client->core, client->read_timeout_ms);
        nmbs_set_byte_timeout(&client->core, client->byte_timeout_ms);
        LOG_DBG("Client initialized successfully");
    }
    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_set_timeouts(nmbs_z_client_t *client, int32_t read_timeout_ms, int32_t byte_timeout_ms)
{
    if (client == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, K_FOREVER);
    if (ret != 0) {
        return ret;
    }

    client->read_timeout_ms = read_timeout_ms;
    client->byte_timeout_ms = byte_timeout_ms;
    nmbs_set_read_timeout(&client->core, read_timeout_ms);
    nmbs_set_byte_timeout(&client->core, byte_timeout_ms);
    client_record_error(client, NMBS_ERROR_NONE);

    k_mutex_unlock(&client->lock);
    return 0;
}

int nmbs_z_client_set_target_address(nmbs_z_client_t *client, uint8_t unit_id, k_timeout_t timeout)
{
    if (client == NULL) {
        LOG_ERR("Set target address failed: NULL client");
        return -EINVAL;
    }

    int ret = client_lock(client, timeout);
    if (ret != 0) {
        LOG_DBG("Set target address lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Setting destination RTU address: %u", unit_id);
    nmbs_set_destination_rtu_address(&client->core, unit_id);
    client_record_error(client, NMBS_ERROR_NONE);

    k_mutex_unlock(&client->lock);
    return 0;
}

int nmbs_z_client_read_holding_registers(nmbs_z_client_t *client, uint16_t address,
                                         uint16_t quantity, uint16_t *registers_out,
                                         k_timeout_t lock_timeout)
{
    if (client == NULL || registers_out == NULL) {
        LOG_ERR("Read holding registers failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read holding registers lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Reading holding registers: addr=%u, qty=%u", address, quantity);
    nmbs_error err = nmbs_read_holding_registers(&client->core, address, quantity, registers_out);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read holding registers error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_input_registers(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, uint16_t *registers_out,
                                       k_timeout_t lock_timeout)
{
    if (client == NULL || registers_out == NULL) {
        LOG_ERR("Read input registers failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read input registers lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Reading input registers: addr=%u, qty=%u", address, quantity);
    nmbs_error err = nmbs_read_input_registers(&client->core, address, quantity, registers_out);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read input registers error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_coils(nmbs_z_client_t *client, uint16_t address,
                             uint16_t quantity, nmbs_bitfield coils_out,
                             k_timeout_t lock_timeout)
{
    if (client == NULL || coils_out == NULL) {
        LOG_ERR("Read coils failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read coils lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Reading coils: addr=%u, qty=%u", address, quantity);
    nmbs_error err = nmbs_read_coils(&client->core, address, quantity, coils_out);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read coils error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_discrete_inputs(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, nmbs_bitfield inputs_out,
                                       k_timeout_t lock_timeout)
{
    if (client == NULL || inputs_out == NULL) {
        LOG_ERR("Read discrete inputs failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read discrete inputs lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Reading discrete inputs: addr=%u, qty=%u", address, quantity);
    nmbs_error err = nmbs_read_discrete_inputs(&client->core, address, quantity, inputs_out);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read discrete inputs error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_single_coil(nmbs_z_client_t *client, uint16_t address,
                                    bool value, k_timeout_t lock_timeout)
{
    if (client == NULL) {
        LOG_ERR("Write single coil failed: NULL client");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Write single coil lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Writing single coil: addr=%u, val=%d", address, value ? 1 : 0);
    nmbs_error err = nmbs_write_single_coil(&client->core, address, value);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Write single coil error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_single_register(nmbs_z_client_t *client, uint16_t address,
                                        uint16_t value, k_timeout_t lock_timeout)
{
    if (client == NULL) {
        LOG_ERR("Write single register failed: NULL client");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Write single register lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Writing single register: addr=%u, val=0x%04X", address, value);
    nmbs_error err = nmbs_write_single_register(&client->core, address, value);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Write single register error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_multiple_coils(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, const nmbs_bitfield coils,
                                       k_timeout_t lock_timeout)
{
    if (client == NULL || coils == NULL) {
        LOG_ERR("Write multiple coils failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Write multiple coils lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Writing multiple coils: addr=%u, qty=%u", address, quantity);
    nmbs_error err = nmbs_write_multiple_coils(&client->core, address, quantity, coils);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Write multiple coils error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_multiple_registers(nmbs_z_client_t *client, uint16_t address,
                                           uint16_t quantity, const uint16_t *registers,
                                           k_timeout_t lock_timeout)
{
    if (client == NULL || registers == NULL) {
        LOG_ERR("Write multiple registers failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Write multiple registers lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Writing multiple registers: addr=%u, qty=%u", address, quantity);
    nmbs_error err = nmbs_write_multiple_registers(&client->core, address, quantity, registers);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Write multiple registers error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_file_record(nmbs_z_client_t *client, uint16_t file_number,
                                   uint16_t record_number, uint16_t *registers_out,
                                   uint16_t quantity, k_timeout_t lock_timeout)
{
    if (client == NULL || registers_out == NULL) {
        LOG_ERR("Read file record failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read file record lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Reading file record: file=%u, record=%u, qty=%u", file_number, record_number, quantity);
    nmbs_error err = nmbs_read_file_record(&client->core, file_number, record_number, registers_out, quantity);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read file record error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_file_record(nmbs_z_client_t *client, uint16_t file_number,
                                    uint16_t record_number, const uint16_t *registers,
                                    uint16_t quantity, k_timeout_t lock_timeout)
{
    if (client == NULL || registers == NULL) {
        LOG_ERR("Write file record failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Write file record lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Writing file record: file=%u, record=%u, qty=%u", file_number, record_number, quantity);
    nmbs_error err = nmbs_write_file_record(&client->core, file_number, record_number, registers, quantity);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Write file record error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_write_registers(nmbs_z_client_t *client, uint16_t read_address,
                                       uint16_t read_quantity, uint16_t *registers_out,
                                       uint16_t write_address, uint16_t write_quantity,
                                       const uint16_t *registers, k_timeout_t lock_timeout)
{
    if (client == NULL || registers_out == NULL || registers == NULL) {
        LOG_ERR("Read/write registers failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read/write registers lock failed: %d", ret);
        return ret;
    }

    LOG_DBG("Read/write registers: rd=%u/%u, wr=%u/%u", read_address, read_quantity, write_address,
            write_quantity);
    nmbs_error err = nmbs_read_write_registers(&client->core, read_address, read_quantity, registers_out,
                                               write_address, write_quantity, registers);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read/write registers error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_device_identification_basic(nmbs_z_client_t *client,
                                                   char *vendor_name, char *product_code,
                                                   char *major_minor_revision,
                                                   uint8_t buffers_length,
                                                   k_timeout_t lock_timeout)
{
    if (client == NULL || vendor_name == NULL || product_code == NULL || major_minor_revision == NULL) {
        LOG_ERR("Read device identification (basic) failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read device identification (basic) lock failed: %d", ret);
        return ret;
    }

    nmbs_error err = nmbs_read_device_identification_basic(&client->core, vendor_name, product_code,
                                                           major_minor_revision, buffers_length);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read device identification (basic) error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_device_identification_regular(nmbs_z_client_t *client,
                                                     char *vendor_url, char *product_name,
                                                     char *model_name,
                                                     char *user_application_name,
                                                     uint8_t buffers_length,
                                                     k_timeout_t lock_timeout)
{
    if (client == NULL || vendor_url == NULL || product_name == NULL || model_name == NULL ||
        user_application_name == NULL) {
        LOG_ERR("Read device identification (regular) failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read device identification (regular) lock failed: %d", ret);
        return ret;
    }

    nmbs_error err = nmbs_read_device_identification_regular(&client->core, vendor_url, product_name,
                                                             model_name, user_application_name,
                                                             buffers_length);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read device identification (regular) error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_device_identification_extended(nmbs_z_client_t *client,
                                                      uint8_t object_id_start, uint8_t *ids,
                                                      char **buffers, uint8_t ids_length,
                                                      uint8_t buffer_length,
                                                      uint8_t *objects_count_out,
                                                      k_timeout_t lock_timeout)
{
    if (client == NULL || ids == NULL || buffers == NULL || objects_count_out == NULL) {
        LOG_ERR("Read device identification (extended) failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read device identification (extended) lock failed: %d", ret);
        return ret;
    }

    nmbs_error err = nmbs_read_device_identification_extended(&client->core, object_id_start, ids, buffers,
                                                              ids_length, buffer_length,
                                                              objects_count_out);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read device identification (extended) error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_device_identification(nmbs_z_client_t *client, uint8_t object_id,
                                             char *buffer, uint8_t buffer_length,
                                             k_timeout_t lock_timeout)
{
    if (client == NULL || buffer == NULL) {
        LOG_ERR("Read device identification failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Read device identification lock failed: %d", ret);
        return ret;
    }

    nmbs_error err = nmbs_read_device_identification(&client->core, object_id, buffer, buffer_length);
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Read device identification error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_raw_pdu(nmbs_z_client_t *client, uint8_t fc, const uint8_t *data,
                          uint16_t data_length, uint8_t *response,
                          uint8_t response_length, k_timeout_t lock_timeout)
{
    if (client == NULL || (data == NULL && data_length > 0)) {
        LOG_ERR("Raw PDU failed: NULL parameter");
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        LOG_DBG("Raw PDU lock failed: %d", ret);
        return ret;
    }

    /* Send and receive under one lock: a separate send/receive pair would let
     * another thread interleave its own transaction on the same transport.
     */
    nmbs_error err = nmbs_send_raw_pdu(&client->core, fc, data, data_length);
    if (err == NMBS_ERROR_NONE) {
        err = nmbs_receive_raw_pdu_response(&client->core, response, response_length);
    }
    client_record_error(client, err);
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Raw PDU (fc 0x%02X) error: %d", fc, err);
    }

    return nmbs_z_error_to_errno(err);
}

#endif /* CONFIG_NANOMODBUS_CLIENT */
