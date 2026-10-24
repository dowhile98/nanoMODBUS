/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z.h>

int nmbs_z_error_to_errno(nmbs_error err)
{
    switch (err) {
    case NMBS_ERROR_NONE:
        return 0;
    case NMBS_ERROR_INVALID_ARGUMENT:
        return -EINVAL;
    case NMBS_ERROR_INVALID_RESPONSE:
        return -EBADMSG;
    case NMBS_ERROR_TIMEOUT:
        return -ETIMEDOUT;
    case NMBS_ERROR_TRANSPORT:
        return -EIO;
    case NMBS_ERROR_CRC:
        return -EBADMSG;
    case NMBS_ERROR_INVALID_TCP_MBAP:
        return -EPROTO;
    case NMBS_ERROR_INVALID_UNIT_ID:
        return -ENODEV;
    case NMBS_ERROR_INVALID_REQUEST:
        return -EINVAL;
    case NMBS_EXCEPTION_ILLEGAL_FUNCTION:
        return -ENOSYS;
    case NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS:
        return -EFAULT;
    case NMBS_EXCEPTION_ILLEGAL_DATA_VALUE:
        return -EDOM;
    case NMBS_EXCEPTION_SERVER_DEVICE_FAILURE:
        return -EIO;
    default:
        return -EPROTO;
    }
}

const char *nmbs_z_strerror(nmbs_error err)
{
    switch (err) {
    case NMBS_ERROR_NONE:
        return "No error";
    case NMBS_ERROR_INVALID_ARGUMENT:
        return "Invalid argument";
    case NMBS_ERROR_INVALID_RESPONSE:
        return "Invalid response from server";
    case NMBS_ERROR_TIMEOUT:
        return "Read/write timeout";
    case NMBS_ERROR_TRANSPORT:
        return "Transport I/O error";
    case NMBS_ERROR_CRC:
        return "CRC check failed";
    case NMBS_ERROR_INVALID_TCP_MBAP:
        return "Invalid TCP MBAP header";
    case NMBS_ERROR_INVALID_UNIT_ID:
        return "Invalid unit ID";
    case NMBS_ERROR_INVALID_REQUEST:
        return "Invalid request from client";
    case NMBS_EXCEPTION_ILLEGAL_FUNCTION:
        return "Modbus exception: Illegal function (01)";
    case NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS:
        return "Modbus exception: Illegal data address (02)";
    case NMBS_EXCEPTION_ILLEGAL_DATA_VALUE:
        return "Modbus exception: Illegal data value (03)";
    case NMBS_EXCEPTION_SERVER_DEVICE_FAILURE:
        return "Modbus exception: Server device failure (04)";
    default:
        return "Unknown error";
    }
}
