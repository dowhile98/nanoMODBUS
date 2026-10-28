/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/nanomodbus/nanomodbus_z.h>

int nmbs_z_error_to_errno(nmbs_error err) {
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
        case NMBS_EXCEPTION_ACKNOWLEDGE:
            return -EAGAIN;
        case NMBS_EXCEPTION_SERVER_DEVICE_BUSY:
            return -EBUSY;
        case NMBS_EXCEPTION_MEMORY_PARITY_ERROR:
            return -EBADMSG;
        case NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE:
            return -ENETUNREACH;
        case NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND:
            return -ETIMEDOUT;
        default:
            return -EPROTO;
    }
}

nmbs_error nmbs_z_errno_to_error(int err) {
    if (err < 0) {
        err = -err;
    }

    switch (err) {
        case 0:
            return NMBS_ERROR_NONE;
        case EINVAL:
            return NMBS_ERROR_INVALID_ARGUMENT;
        case ENODEV:
            return NMBS_ERROR_INVALID_UNIT_ID;
        case EPROTO:
            return NMBS_ERROR_INVALID_TCP_MBAP;
        case ENOSYS:
            return NMBS_EXCEPTION_ILLEGAL_FUNCTION;
        case EFAULT:
            return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        case EDOM:
            return NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        case EIO:
            return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        case EAGAIN:
            return NMBS_EXCEPTION_ACKNOWLEDGE;
        case EBUSY:
            return NMBS_EXCEPTION_SERVER_DEVICE_BUSY;
        case EBADMSG:
            return NMBS_EXCEPTION_MEMORY_PARITY_ERROR;
        case ENETUNREACH:
            return NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE;
        case ETIMEDOUT:
            return NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND;
        default:
            return NMBS_ERROR_TRANSPORT;
    }
}

nmbs_error errno_to_nmbs_error(int err) {
    return nmbs_z_errno_to_error(err);
}

const char* nmbs_z_strerror(nmbs_error err) {
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
        case NMBS_EXCEPTION_ACKNOWLEDGE:
            return "Modbus exception: Acknowledge (05)";
        case NMBS_EXCEPTION_SERVER_DEVICE_BUSY:
            return "Modbus exception: Server device busy (06)";
        case NMBS_EXCEPTION_MEMORY_PARITY_ERROR:
            return "Modbus exception: Memory parity error (08)";
        case NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE:
            return "Modbus exception: Gateway path unavailable (10)";
        case NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND:
            return "Modbus exception: Gateway target device failed to respond (11)";
        default:
            return "Unknown error";
    }
}
