/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <nanomodbus.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/ztest.h>

ZTEST(nanomodbus_error, test_error_translation_matrix) {
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
    zassert_equal(nmbs_z_error_to_errno(NMBS_ERROR_INVALID_REQUEST), -EINVAL, "-8 -> -EINVAL");

    /* Modbus Exceptions (positive values) */
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_FUNCTION), -ENOSYS, "1 -> -ENOSYS");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS), -EFAULT, "2 -> -EFAULT");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ILLEGAL_DATA_VALUE), -EDOM, "3 -> -EDOM");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_SERVER_DEVICE_FAILURE), -EIO, "4 -> -EIO");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_ACKNOWLEDGE), -EAGAIN, "5 -> -EAGAIN");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_SERVER_DEVICE_BUSY), -EBUSY, "6 -> -EBUSY");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_MEMORY_PARITY_ERROR), -EBADMSG, "8 -> -EBADMSG");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE), -ENETUNREACH, "10 -> -ENETUNREACH");
    zassert_equal(nmbs_z_error_to_errno(NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND), -ETIMEDOUT,
                  "11 -> -ETIMEDOUT");

    /* Reverse mapping: POSIX errno -> nmbs_error */
    zassert_equal(nmbs_z_errno_to_error(0), NMBS_ERROR_NONE, "0 -> NMBS_ERROR_NONE");
    zassert_equal(nmbs_z_errno_to_error(-ENOSYS), NMBS_EXCEPTION_ILLEGAL_FUNCTION, "-ENOSYS -> 1");
    zassert_equal(nmbs_z_errno_to_error(-EFAULT), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "-EFAULT -> 2");
    zassert_equal(nmbs_z_errno_to_error(-EDOM), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE, "-EDOM -> 3");
    zassert_equal(nmbs_z_errno_to_error(-EIO), NMBS_EXCEPTION_SERVER_DEVICE_FAILURE, "-EIO -> 4");
    zassert_equal(nmbs_z_errno_to_error(-EAGAIN), NMBS_EXCEPTION_ACKNOWLEDGE, "-EAGAIN -> 5");
    zassert_equal(nmbs_z_errno_to_error(-EBUSY), NMBS_EXCEPTION_SERVER_DEVICE_BUSY, "-EBUSY -> 6");
    zassert_equal(nmbs_z_errno_to_error(-EBADMSG), NMBS_EXCEPTION_MEMORY_PARITY_ERROR, "-EBADMSG -> 8");
    zassert_equal(nmbs_z_errno_to_error(-ENETUNREACH), NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE, "-ENETUNREACH -> 10");
    zassert_equal(nmbs_z_errno_to_error(-ETIMEDOUT), NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND,
                  "-ETIMEDOUT -> 11");

    /* Test alias errno_to_nmbs_error */
    zassert_equal(errno_to_nmbs_error(-EAGAIN), NMBS_EXCEPTION_ACKNOWLEDGE, "errno_to_nmbs_error -EAGAIN -> 5");
    zassert_equal(errno_to_nmbs_error(-EBUSY), NMBS_EXCEPTION_SERVER_DEVICE_BUSY, "errno_to_nmbs_error -EBUSY -> 6");
    zassert_equal(errno_to_nmbs_error(-EBADMSG), NMBS_EXCEPTION_MEMORY_PARITY_ERROR,
                  "errno_to_nmbs_error -EBADMSG -> 8");
    zassert_equal(errno_to_nmbs_error(-ENETUNREACH), NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE,
                  "errno_to_nmbs_error -ENETUNREACH -> 10");
    zassert_equal(errno_to_nmbs_error(-ETIMEDOUT), NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND,
                  "errno_to_nmbs_error -ETIMEDOUT -> 11");

    /* Unknown code fallback */
    zassert_equal(nmbs_z_error_to_errno((nmbs_error) -999), -EPROTO, "unknown -> -EPROTO");
    zassert_equal(nmbs_z_error_to_errno((nmbs_error) 999), -EPROTO, "unknown positive -> -EPROTO");

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
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_ACKNOWLEDGE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_SERVER_DEVICE_BUSY), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_MEMORY_PARITY_ERROR), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE), "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror(NMBS_EXCEPTION_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND),
                     "strerror must not be NULL");
    zassert_not_null(nmbs_z_strerror((nmbs_error) -999), "strerror must not be NULL");
}

ZTEST_SUITE(nanomodbus_error, NULL, NULL, NULL, NULL, NULL);
