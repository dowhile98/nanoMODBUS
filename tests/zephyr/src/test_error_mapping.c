/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <nanomodbus.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>

ZTEST(nanomodbus_error, test_error_translation_matrix)
{
    /* Success */
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_NONE), 0, "NMBS_ERROR_NONE -> 0");

    /* Internal errors mapped to negative POSIX errno */
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_ARGUMENT), -EINVAL, "-1 -> -EINVAL");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_RESPONSE), -EBADMSG, "-2 -> -EBADMSG");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_TIMEOUT), -ETIMEDOUT, "-3 -> -ETIMEDOUT");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_TRANSPORT), -EIO, "-4 -> -EIO");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_CRC), -EBADMSG, "-5 -> -EBADMSG");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_TCP_MBAP), -EPROTO, "-6 -> -EPROTO");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_UNIT_ID), -ENODEV, "-7 -> -ENODEV");
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_REQUEST), -EBADMSG, "-8 -> -EBADMSG");

    /* Modbus Exceptions (positive values) */
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_FUNCTION), -ENOSYS, "1 -> -ENOSYS");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS), -EFAULT, "2 -> -EFAULT");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_DATA_VALUE), -EDOM, "3 -> -EDOM");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_SERVER_DEVICE_FAILURE), -EIO, "4 -> -EIO");

    /* Unknown code fallback */
    zassert_equal(nmbs_z_error_to_errno((nmbs_error)-999), -EPROTO, "unknown -> -EPROTO");
    zassert_equal(nmbs_z_error_to_errno((nmbs_error)999), -EPROTO, "unknown positive -> -EPROTO");

    /* String representation */
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_NONE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_INVALID_ARGUMENT), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_INVALID_RESPONSE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_TIMEOUT), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_TRANSPORT), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_CRC), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_INVALID_TCP_MBAP), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_INVALID_UNIT_ID), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_ERROR_INVALID_REQUEST), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_ILLEGAL_FUNCTION), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_ILLEGAL_DATA_VALUE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_SERVER_DEVICE_FAILURE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror((nmbs_error)-999), "strerror must not be NULL");
}

ZTEST_SUITE(nanomodbus_error, NULL, NULL, NULL, NULL, NULL);
