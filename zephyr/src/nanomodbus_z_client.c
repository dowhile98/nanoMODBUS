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
        if (ret == -EAGAIN) {
            LOG_DBG("Client lock contention: timeout waiting for mutex");
            return -ETIMEDOUT;
        }
        LOG_DBG("Client lock contention: %d", ret);
        return ret;
    }
    LOG_DBG("Client lock acquired");
    return 0;
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

    nmbs_error err = nmbs_client_create(&client->core, &client->platform);
    if (err != NMBS_ERROR_NONE) {
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
    k_mutex_unlock(&client->lock);

    if (err != NMBS_ERROR_NONE) {
        LOG_ERR("Write multiple registers error: %d", err);
    }

    return nmbs_z_error_to_errno(err);
}

#endif /* CONFIG_NANOMODBUS_CLIENT */
