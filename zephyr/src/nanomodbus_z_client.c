/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <errno.h>

#ifdef CONFIG_NANOMODBUS_CLIENT

static inline int client_lock(nmbs_z_client_t *client, k_timeout_t lock_timeout)
{
    int ret = k_mutex_lock(&client->lock, lock_timeout);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            return -ETIMEDOUT;
        }
        return ret;
    }
    return 0;
}

int nmbs_z_client_init(nmbs_z_client_t *client, const nmbs_platform_conf *transport)
{
    if (client == NULL || transport == NULL) {
        return -EINVAL;
    }

    k_mutex_init(&client->lock);
    client->platform = *transport;

    nmbs_error err = nmbs_client_create(&client->core, &client->platform);
    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_set_target_address(nmbs_z_client_t *client, uint8_t unit_id, k_timeout_t timeout)
{
    if (client == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_set_destination_rtu_address(&client->core, unit_id);

    k_mutex_unlock(&client->lock);
    return 0;
}

int nmbs_z_client_read_holding_registers(nmbs_z_client_t *client, uint16_t address,
                                         uint16_t quantity, uint16_t *registers_out,
                                         k_timeout_t lock_timeout)
{
    if (client == NULL || registers_out == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_read_holding_registers(&client->core, address, quantity, registers_out);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_input_registers(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, uint16_t *registers_out,
                                       k_timeout_t lock_timeout)
{
    if (client == NULL || registers_out == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_read_input_registers(&client->core, address, quantity, registers_out);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_coils(nmbs_z_client_t *client, uint16_t address,
                             uint16_t quantity, nmbs_bitfield coils_out,
                             k_timeout_t lock_timeout)
{
    if (client == NULL || coils_out == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_read_coils(&client->core, address, quantity, coils_out);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_read_discrete_inputs(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, nmbs_bitfield inputs_out,
                                       k_timeout_t lock_timeout)
{
    if (client == NULL || inputs_out == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_read_discrete_inputs(&client->core, address, quantity, inputs_out);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_single_coil(nmbs_z_client_t *client, uint16_t address,
                                    bool value, k_timeout_t lock_timeout)
{
    if (client == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_write_single_coil(&client->core, address, value);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_single_register(nmbs_z_client_t *client, uint16_t address,
                                        uint16_t value, k_timeout_t lock_timeout)
{
    if (client == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_write_single_register(&client->core, address, value);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_multiple_coils(nmbs_z_client_t *client, uint16_t address,
                                       uint16_t quantity, const nmbs_bitfield coils,
                                       k_timeout_t lock_timeout)
{
    if (client == NULL || coils == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_write_multiple_coils(&client->core, address, quantity, coils);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

int nmbs_z_client_write_multiple_registers(nmbs_z_client_t *client, uint16_t address,
                                           uint16_t quantity, const uint16_t *registers,
                                           k_timeout_t lock_timeout)
{
    if (client == NULL || registers == NULL) {
        return -EINVAL;
    }

    int ret = client_lock(client, lock_timeout);
    if (ret != 0) {
        return ret;
    }

    nmbs_error err = nmbs_write_multiple_registers(&client->core, address, quantity, registers);
    k_mutex_unlock(&client->lock);

    return nmbs_z_error_to_errno(err);
}

#endif /* CONFIG_NANOMODBUS_CLIENT */
