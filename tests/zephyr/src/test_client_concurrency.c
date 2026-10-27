/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <string.h>

#define MOCK_REGS_COUNT 64

static uint16_t mock_server_holding_regs[MOCK_REGS_COUNT];
static uint16_t mock_server_input_regs[MOCK_REGS_COUNT];
static uint8_t mock_server_coils[8];
static uint8_t mock_server_discrete_inputs[8];

static uint8_t mock_rx_buf[256];
static uint16_t mock_rx_len;
static uint16_t mock_rx_pos;

/* When non-zero, the mock answers the next request with this Modbus exception
 * code (FC | 0x80) instead of a normal response.
 */
static uint8_t mock_exception_code;

/* Single-file record store used by the FC 20/21 wrappers. */
#define MOCK_FILE_REGS_COUNT 16
static uint16_t mock_file_regs[MOCK_FILE_REGS_COUNT];
static uint16_t mock_file_number;
static uint16_t mock_file_record;
static uint16_t mock_file_count;

static void mock_append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = nmbs_crc_calc(buf, len, NULL);
    buf[len] = (uint8_t)((crc >> 8) & 0xFF);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
}

static int32_t mock_transport_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);
    ARG_UNUSED(arg);

    if (mock_rx_pos + count > mock_rx_len) {
        return 0; /* Simulates read timeout / insufficient bytes */
    }

    memcpy(buf, &mock_rx_buf[mock_rx_pos], count);
    mock_rx_pos += count;
    return count;
}

static int32_t mock_transport_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);
    ARG_UNUSED(arg);

    if (count < 4) {
        return count;
    }

    uint8_t unit_id = buf[0];
    uint8_t fc = buf[1];

    mock_rx_pos = 0;
    mock_rx_len = 0;

    if (mock_exception_code != 0) {
        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = (uint8_t)(fc | 0x80);
        mock_rx_buf[2] = mock_exception_code;
        mock_append_crc(mock_rx_buf, 3);
        mock_rx_len = 5;

        k_yield();
        return count;
    }

    if (fc == 0x01) { /* Read Coils */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint8_t bytes = (uint8_t)((qty + 7) / 8);

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x01;
        mock_rx_buf[2] = bytes;
        for (uint8_t b = 0; b < bytes; b++) {
            uint8_t byte_val = 0;
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t idx = addr + b * 8 + bit;
                if (idx < addr + qty && idx < sizeof(mock_server_coils) * 8) {
                    if (mock_server_coils[idx / 8] & (1 << (idx % 8))) {
                        byte_val |= (1 << bit);
                    }
                }
            }
            mock_rx_buf[3 + b] = byte_val;
        }
        mock_append_crc(mock_rx_buf, 3 + bytes);
        mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x02) { /* Read Discrete Inputs */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint8_t bytes = (uint8_t)((qty + 7) / 8);

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x02;
        mock_rx_buf[2] = bytes;
        for (uint8_t b = 0; b < bytes; b++) {
            uint8_t byte_val = 0;
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t idx = addr + b * 8 + bit;
                if (idx < addr + qty && idx < sizeof(mock_server_discrete_inputs) * 8) {
                    if (mock_server_discrete_inputs[idx / 8] & (1 << (idx % 8))) {
                        byte_val |= (1 << bit);
                    }
                }
            }
            mock_rx_buf[3 + b] = byte_val;
        }
        mock_append_crc(mock_rx_buf, 3 + bytes);
        mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x03) { /* Read Holding Registers */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint8_t bytes = (uint8_t)(qty * 2);

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x03;
        mock_rx_buf[2] = bytes;
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t reg_val = 0;
            if (addr + i < MOCK_REGS_COUNT) {
                reg_val = mock_server_holding_regs[addr + i];
            }
            mock_rx_buf[3 + 2 * i] = (uint8_t)((reg_val >> 8) & 0xFF);
            mock_rx_buf[4 + 2 * i] = (uint8_t)(reg_val & 0xFF);
        }
        mock_append_crc(mock_rx_buf, 3 + bytes);
        mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x04) { /* Read Input Registers */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint8_t bytes = (uint8_t)(qty * 2);

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x04;
        mock_rx_buf[2] = bytes;
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t reg_val = 0;
            if (addr + i < MOCK_REGS_COUNT) {
                reg_val = mock_server_input_regs[addr + i];
            }
            mock_rx_buf[3 + 2 * i] = (uint8_t)((reg_val >> 8) & 0xFF);
            mock_rx_buf[4 + 2 * i] = (uint8_t)(reg_val & 0xFF);
        }
        mock_append_crc(mock_rx_buf, 3 + bytes);
        mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x05) { /* Write Single Coil */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t val = ((uint16_t)buf[4] << 8) | buf[5];
        if (addr < sizeof(mock_server_coils) * 8) {
            if (val == 0xFF00) {
                mock_server_coils[addr / 8] |= (1 << (addr % 8));
            } else {
                mock_server_coils[addr / 8] &= ~(1 << (addr % 8));
            }
        }
        memcpy(mock_rx_buf, buf, 6);
        mock_append_crc(mock_rx_buf, 6);
        mock_rx_len = 8;
    } else if (fc == 0x06) { /* Write Single Register */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t val = ((uint16_t)buf[4] << 8) | buf[5];
        if (addr < MOCK_REGS_COUNT) {
            mock_server_holding_regs[addr] = val;
        }
        memcpy(mock_rx_buf, buf, 6);
        mock_append_crc(mock_rx_buf, 6);
        mock_rx_len = 8;
    } else if (fc == 0x0F) { /* Write Multiple Coils */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        for (uint16_t i = 0; i < qty; i++) {
            if (addr + i < sizeof(mock_server_coils) * 8) {
                uint8_t b = buf[7 + i / 8];
                if (b & (1 << (i % 8))) {
                    mock_server_coils[(addr + i) / 8] |= (1 << ((addr + i) % 8));
                } else {
                    mock_server_coils[(addr + i) / 8] &= ~(1 << ((addr + i) % 8));
                }
            }
        }
        memcpy(mock_rx_buf, buf, 6);
        mock_append_crc(mock_rx_buf, 6);
        mock_rx_len = 8;
    } else if (fc == 0x10) { /* Write Multiple Registers */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        for (uint16_t i = 0; i < qty; i++) {
            if (addr + i < MOCK_REGS_COUNT) {
                mock_server_holding_regs[addr + i] = ((uint16_t)buf[7 + 2 * i] << 8) | buf[8 + 2 * i];
            }
        }
        memcpy(mock_rx_buf, buf, 6);
        mock_append_crc(mock_rx_buf, 6);
        mock_rx_len = 8;
    } else if (fc == 0x14) { /* Read File Record */
        uint16_t file = ((uint16_t)buf[4] << 8) | buf[5];
        uint16_t record = ((uint16_t)buf[6] << 8) | buf[7];
        uint16_t qty = ((uint16_t)buf[8] << 8) | buf[9];
        uint8_t bytes = (uint8_t)(qty * 2);
        bool known_record = (file == mock_file_number) && (record == mock_file_record);

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x14;
        mock_rx_buf[2] = (uint8_t)(2 + bytes);
        mock_rx_buf[3] = (uint8_t)(1 + bytes);
        mock_rx_buf[4] = 0x06;
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t val = (known_record && i < mock_file_count) ? mock_file_regs[i] : 0;
            mock_rx_buf[5 + 2 * i] = (uint8_t)((val >> 8) & 0xFF);
            mock_rx_buf[6 + 2 * i] = (uint8_t)(val & 0xFF);
        }
        mock_append_crc(mock_rx_buf, 5 + bytes);
        mock_rx_len = 5 + bytes + 2;
    } else if (fc == 0x15) { /* Write File Record */
        uint16_t file = ((uint16_t)buf[4] << 8) | buf[5];
        uint16_t record = ((uint16_t)buf[6] << 8) | buf[7];
        uint16_t qty = ((uint16_t)buf[8] << 8) | buf[9];

        mock_file_number = file;
        mock_file_record = record;
        mock_file_count = qty;
        for (uint16_t i = 0; i < qty && i < MOCK_FILE_REGS_COUNT; i++) {
            mock_file_regs[i] = ((uint16_t)buf[10 + 2 * i] << 8) | buf[11 + 2 * i];
        }

        memcpy(mock_rx_buf, buf, 10 + qty * 2);
        mock_append_crc(mock_rx_buf, (uint16_t)(10 + qty * 2));
        mock_rx_len = (uint16_t)(10 + qty * 2 + 2);
    } else if (fc == 0x17) { /* Read/Write Multiple Registers */
        uint16_t read_addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t read_qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint16_t write_addr = ((uint16_t)buf[6] << 8) | buf[7];
        uint16_t write_qty = ((uint16_t)buf[8] << 8) | buf[9];
        uint8_t bytes = (uint8_t)(read_qty * 2);

        for (uint16_t i = 0; i < write_qty; i++) {
            if (write_addr + i < MOCK_REGS_COUNT) {
                mock_server_holding_regs[write_addr + i] =
                    ((uint16_t)buf[11 + 2 * i] << 8) | buf[12 + 2 * i];
            }
        }

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x17;
        mock_rx_buf[2] = bytes;
        for (uint16_t i = 0; i < read_qty; i++) {
            uint16_t val = 0;
            if (read_addr + i < MOCK_REGS_COUNT) {
                val = mock_server_holding_regs[read_addr + i];
            }
            mock_rx_buf[3 + 2 * i] = (uint8_t)((val >> 8) & 0xFF);
            mock_rx_buf[4 + 2 * i] = (uint8_t)(val & 0xFF);
        }
        mock_append_crc(mock_rx_buf, 3 + bytes);
        mock_rx_len = 3 + bytes + 2;
    } else if (fc == 0x2B) { /* Read Device Identification (MEI 0x0E) */
        static const char vendor_name[] = "nanoMODBUS";
        static const char product_code[] = "nmb-z";
        const char *value = NULL;
        uint8_t object_id = buf[4];
        uint8_t value_len;

        if (object_id == 0x00) {
            value = vendor_name;
        } else if (object_id == 0x01) {
            value = product_code;
        } else {
            value = "";
        }
        value_len = (uint8_t)strlen(value);

        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x2B;
        mock_rx_buf[2] = 0x0E; /* MEI type */
        mock_rx_buf[3] = 0x04; /* read device id code: single object */
        mock_rx_buf[4] = 0x01; /* conformity level: basic */
        mock_rx_buf[5] = 0x00; /* no more follows */
        mock_rx_buf[6] = 0x7F; /* next object id */
        mock_rx_buf[7] = 0x01; /* object count */
        mock_rx_buf[8] = object_id;
        mock_rx_buf[9] = value_len;
        memcpy(&mock_rx_buf[10], value, value_len);
        mock_append_crc(mock_rx_buf, (uint16_t)(10 + value_len));
        mock_rx_len = (uint16_t)(10 + value_len + 2);
    } else if (fc == 0x41) { /* Raw PDU echo used by the tests */
        mock_rx_buf[0] = unit_id;
        mock_rx_buf[1] = 0x41;
        memcpy(&mock_rx_buf[2], &buf[2], 2);
        mock_rx_buf[4] = 0xAA;
        mock_rx_buf[5] = 0x55;
        mock_append_crc(mock_rx_buf, 6);
        mock_rx_len = 8;
    }

    /* Small yield to give other threads a scheduling window */
    k_yield();

    return count;
}

static void test_client_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(mock_server_holding_regs, 0, sizeof(mock_server_holding_regs));
    memset(mock_server_input_regs, 0, sizeof(mock_server_input_regs));
    memset(mock_server_coils, 0, sizeof(mock_server_coils));
    memset(mock_server_discrete_inputs, 0, sizeof(mock_server_discrete_inputs));
    memset(mock_file_regs, 0, sizeof(mock_file_regs));
    mock_exception_code = 0;
    mock_file_number = 0;
    mock_file_record = 0;
    mock_file_count = 0;
    mock_rx_len = 0;
    mock_rx_pos = 0;
}

/* ========================================================================= */
/* Test 1: Single-thread read/write round-trip success                       */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_single_thread_roundtrip)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");

    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    /* Single register write and read */
    ret = nmbs_z_client_write_single_register(&client, 5, 0x1234, K_MSEC(100));
    zassert_equal(ret, 0, "Write single register failed: %d", ret);

    uint16_t reg_val = 0;
    ret = nmbs_z_client_read_holding_registers(&client, 5, 1, &reg_val, K_MSEC(100));
    zassert_equal(ret, 0, "Read holding registers failed: %d", ret);
    zassert_equal(reg_val, 0x1234, "Read value mismatch: 0x%04X != 0x1234", reg_val);

    /* Multiple registers write and read */
    uint16_t write_vals[3] = {0xAAAA, 0xBBBB, 0xCCCC};
    ret = nmbs_z_client_write_multiple_registers(&client, 10, 3, write_vals, K_MSEC(100));
    zassert_equal(ret, 0, "Write multiple registers failed: %d", ret);

    uint16_t read_vals[3] = {0};
    ret = nmbs_z_client_read_holding_registers(&client, 10, 3, read_vals, K_MSEC(100));
    zassert_equal(ret, 0, "Read multiple registers failed: %d", ret);
    zassert_equal(read_vals[0], 0xAAAA, "Mismatch reg 0");
    zassert_equal(read_vals[1], 0xBBBB, "Mismatch reg 1");
    zassert_equal(read_vals[2], 0xCCCC, "Mismatch reg 2");

    /* Coils and inputs test */
    ret = nmbs_z_client_write_single_coil(&client, 2, true, K_MSEC(100));
    zassert_equal(ret, 0, "Write single coil failed: %d", ret);

    nmbs_bitfield coil_bits = {0};
    ret = nmbs_z_client_read_coils(&client, 0, 8, coil_bits, K_MSEC(100));
    zassert_equal(ret, 0, "Read coils failed: %d", ret);
    zassert_true(nmbs_bitfield_read(coil_bits, 2), "Coil 2 must be true");
}

/* ========================================================================= */
/* Test 2: Concurrency test with parallel Zephyr threads                    */
/* ========================================================================= */
#define NUM_WORKER_THREADS 3
#define THREAD_ITERATIONS 25
#define WORKER_STACK_SIZE 2048

static K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, NUM_WORKER_THREADS, WORKER_STACK_SIZE);
static struct k_thread worker_threads[NUM_WORKER_THREADS];
static atomic_t worker_errors = ATOMIC_INIT(0);

struct worker_arg {
    nmbs_z_client_t *client;
    uint16_t reg_address;
    uint16_t base_value;
};

static struct worker_arg worker_args[NUM_WORKER_THREADS];

static void worker_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    struct worker_arg *arg = (struct worker_arg *)p1;

    for (int i = 0; i < THREAD_ITERATIONS; i++) {
        uint16_t val_to_write = (uint16_t)(arg->base_value + i);

        int ret = nmbs_z_client_write_single_register(arg->client, arg->reg_address,
                                                      val_to_write, K_MSEC(200));
        if (ret != 0) {
            atomic_inc(&worker_errors);
            continue;
        }

        uint16_t val_read = 0;
        ret = nmbs_z_client_read_holding_registers(arg->client, arg->reg_address,
                                                   1, &val_read, K_MSEC(200));
        if (ret != 0 || val_read != val_to_write) {
            atomic_inc(&worker_errors);
            continue;
        }

        k_yield();
    }
}

ZTEST(nanomodbus_client, test_client_parallel_threads_concurrency)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");

    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    atomic_set(&worker_errors, 0);

    for (int t = 0; t < NUM_WORKER_THREADS; t++) {
        worker_args[t].client = &client;
        worker_args[t].reg_address = (uint16_t)(10 * (t + 1));
        worker_args[t].base_value = (uint16_t)(0x1000 * (t + 1));

        k_thread_create(&worker_threads[t],
                        worker_stacks[t],
                        WORKER_STACK_SIZE,
                        worker_thread_fn,
                        &worker_args[t], NULL, NULL,
                        5 + t, 0, K_NO_WAIT);
    }

    for (int t = 0; t < NUM_WORKER_THREADS; t++) {
        k_thread_join(&worker_threads[t], K_FOREVER);
    }

    zassert_equal(atomic_get(&worker_errors), 0,
                  "All concurrent worker threads must complete without error");

    /* Verify final register state */
    for (int t = 0; t < NUM_WORKER_THREADS; t++) {
        uint16_t final_val = 0;
        ret = nmbs_z_client_read_holding_registers(&client, worker_args[t].reg_address,
                                                   1, &final_val, K_MSEC(100));
        zassert_equal(ret, 0, "Final read failed");
        uint16_t expected = (uint16_t)(worker_args[t].base_value + THREAD_ITERATIONS - 1);
        zassert_equal(final_val, expected,
                      "Final register %d mismatch: got 0x%04X expected 0x%04X",
                      worker_args[t].reg_address, final_val, expected);
    }
}

/* ========================================================================= */
/* Test 3 (Review Focus 1): Lock timeout contention and deadlock prevention  */
/* ========================================================================= */
static K_SEM_DEFINE(sem_lock_held, 0, 1);
static K_SEM_DEFINE(sem_release_lock, 0, 1);
static K_THREAD_STACK_DEFINE(holder_stack, 4096);
static struct k_thread holder_thread;

static void lock_holder_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    nmbs_z_client_t *client = (nmbs_z_client_t *)p1;

    int ret = k_mutex_lock(&client->lock, K_FOREVER);
    zassert_equal(ret, 0, "Holder thread failed to acquire lock");

    k_sem_give(&sem_lock_held);

    /* Hold mutex until released */
    k_sem_take(&sem_release_lock, K_FOREVER);
    k_mutex_unlock(&client->lock);
}

ZTEST(nanomodbus_client, test_client_lock_contention_timeout)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");

    k_sem_reset(&sem_lock_held);
    k_sem_reset(&sem_release_lock);

    /* Launch holder thread which artificially keeps the lock */
    k_thread_create(&holder_thread,
                    holder_stack,
                    K_THREAD_STACK_SIZEOF(holder_stack),
                    lock_holder_thread,
                    &client, NULL, NULL,
                    4, 0, K_NO_WAIT);

    /* Wait until the other thread holds the mutex */
    k_sem_take(&sem_lock_held, K_FOREVER);

    /* Attempt operation with K_NO_WAIT -> must fail with -EBUSY */
    uint16_t reg_val = 0;
    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, &reg_val, K_NO_WAIT);
    zassert_true(ret == -EBUSY || ret == -ETIMEDOUT,
                 "Expected -EBUSY or -ETIMEDOUT on K_NO_WAIT contention, got %d", ret);
    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_ERROR_NONE,
                  "Lock contention must not mutate last_error without holding lock, got %d",
                  nmbs_z_client_get_last_error(&client));

    /* Attempt operation with short timeout -> must fail with -ETIMEDOUT or -EBUSY */
    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, &reg_val, K_MSEC(30));
    zassert_true(ret == -EBUSY || ret == -ETIMEDOUT,
                 "Expected -EBUSY or -ETIMEDOUT on timeout contention, got %d", ret);

    /* Attempt set_target_address with short timeout */
    ret = nmbs_z_client_set_target_address(&client, 2, K_MSEC(30));
    zassert_true(ret == -EBUSY || ret == -ETIMEDOUT,
                 "Expected -EBUSY or -ETIMEDOUT on set_target_address contention, got %d", ret);

    /* Signal holder thread to release mutex */
    k_sem_give(&sem_release_lock);
    k_thread_join(&holder_thread, K_FOREVER);

    /* Now that lock is free, operations should succeed deterministically without deadlock */
    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "set_target_address should succeed when lock is free");

    ret = nmbs_z_client_write_single_register(&client, 0, 0x5555, K_MSEC(100));
    zassert_equal(ret, 0, "Write single register should succeed when lock is free");

    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, &reg_val, K_MSEC(100));
    zassert_equal(ret, 0, "Read holding registers should succeed when lock is free");
    zassert_equal(reg_val, 0x5555, "Register value mismatch");
}

ZTEST(nanomodbus_client, test_client_timeout_configuration)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;

    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init failed: %d", ret);

    zassert_equal(client.read_timeout_ms, CONFIG_NANOMODBUS_CLIENT_DEFAULT_READ_TIMEOUT_MS,
                  "Default read timeout not applied");
    zassert_equal(client.byte_timeout_ms, CONFIG_NANOMODBUS_CLIENT_DEFAULT_BYTE_TIMEOUT_MS,
                  "Default byte timeout not applied");
    zassert_equal(client.core.read_timeout_ms, CONFIG_NANOMODBUS_CLIENT_DEFAULT_READ_TIMEOUT_MS,
                  "Core read timeout not applied");
    zassert_equal(client.core.byte_timeout_ms, CONFIG_NANOMODBUS_CLIENT_DEFAULT_BYTE_TIMEOUT_MS,
                  "Core byte timeout not applied");

    ret = nmbs_z_client_set_timeouts(&client, 250, 25);
    zassert_equal(ret, 0, "set_timeouts failed: %d", ret);
    zassert_equal(client.read_timeout_ms, 250, "Read timeout not updated");
    zassert_equal(client.byte_timeout_ms, 25, "Byte timeout not updated");
    zassert_equal(client.core.read_timeout_ms, 250, "Core read timeout not updated");
    zassert_equal(client.core.byte_timeout_ms, 25, "Core byte timeout not updated");

    ret = nmbs_z_client_set_timeouts(NULL, 1, 1);
    zassert_equal(ret, -EINVAL, "NULL client must be rejected");
}

/* ========================================================================= */
/* Test 4 (P2): last_error keeps Modbus exception codes                     */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_last_error_preserves_exception)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");
    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_ERROR_NONE,
                  "Fresh client must not report an error");

    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    uint16_t reg_val = 0;

    /* Exception 2 maps to -EFAULT, which is indistinguishable from a plain
     * bad-address errno. last_error must still carry the exception code.
     */
    mock_exception_code = NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, &reg_val, K_MSEC(100));
    zassert_equal(ret, -EFAULT, "Exception 2 must map to -EFAULT, got %d", ret);
    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS,
                  "last_error lost exception 2: %d", nmbs_z_client_get_last_error(&client));

    /* Exception 1 maps to -ENOSYS. */
    mock_exception_code = NMBS_EXCEPTION_ILLEGAL_FUNCTION;
    ret = nmbs_z_client_write_single_register(&client, 0, 1, K_MSEC(100));
    zassert_equal(ret, -ENOSYS, "Exception 1 must map to -ENOSYS, got %d", ret);
    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_EXCEPTION_ILLEGAL_FUNCTION,
                  "last_error lost exception 1");

    /* A successful request clears the previous error. */
    mock_exception_code = 0;
    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, &reg_val, K_MSEC(100));
    zassert_equal(ret, 0, "Read must succeed once the exception is cleared: %d", ret);
    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_ERROR_NONE,
                  "last_error must be cleared by a successful call");

    zassert_equal(nmbs_z_client_get_last_error(NULL), NMBS_ERROR_INVALID_ARGUMENT,
                  "NULL client must report an invalid argument");
}

/* ========================================================================= */
/* Test 5 (P2): FC 23 read/write multiple registers in one transaction       */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_fc23_read_write_registers)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");
    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    mock_server_holding_regs[30] = 0xA1A1;
    mock_server_holding_regs[31] = 0xB2B2;

    const uint16_t write_vals[2] = {0x1111, 0x2222};
    uint16_t read_vals[2] = {0};

    ret = nmbs_z_client_read_write_registers(&client, 30, 2, read_vals, 20, 2, write_vals,
                                             K_MSEC(100));
    zassert_equal(ret, 0, "FC 23 must succeed: %d", ret);
    zassert_equal(read_vals[0], 0xA1A1, "FC 23 read[0] mismatch: 0x%04X", read_vals[0]);
    zassert_equal(read_vals[1], 0xB2B2, "FC 23 read[1] mismatch: 0x%04X", read_vals[1]);
    zassert_equal(mock_server_holding_regs[20], 0x1111, "FC 23 must write the first register");
    zassert_equal(mock_server_holding_regs[21], 0x2222, "FC 23 must write the second register");
}

/* ========================================================================= */
/* Test 6 (P2): FC 20/21 file record round-trip                               */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_file_record_roundtrip)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");
    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    const uint16_t write_vals[4] = {0xDEAD, 0xBEEF, 0x1234, 0x5678};
    uint16_t read_vals[4] = {0};

    ret = nmbs_z_client_write_file_record(&client, 4, 2, write_vals, 4, K_MSEC(100));
    zassert_equal(ret, 0, "FC 21 must succeed: %d", ret);
    zassert_equal(mock_file_number, 4, "File number not transmitted");
    zassert_equal(mock_file_record, 2, "Record number not transmitted");

    ret = nmbs_z_client_read_file_record(&client, 4, 2, read_vals, 4, K_MSEC(100));
    zassert_equal(ret, 0, "FC 20 must succeed: %d", ret);
    zassert_equal(memcmp(read_vals, write_vals, sizeof(write_vals)), 0,
                  "FC 20/21 round-trip mismatch");
}

/* ========================================================================= */
/* Test 7 (P2): FC 43/14 single-object device identification                 */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_device_identification_single)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");
    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    char vendor[24] = {0};
    ret = nmbs_z_client_read_device_identification(&client, 0x00, vendor, sizeof(vendor),
                                                   K_MSEC(100));
    zassert_equal(ret, 0, "Device identification must succeed: %d", ret);
    zassert_str_equal(vendor, "nanoMODBUS", "VendorName mismatch: '%s'", vendor);
}

/* ========================================================================= */
/* Test 8 (P2): raw PDU send+receive under a single lock acquisition          */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_raw_pdu)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");
    ret = nmbs_z_client_set_target_address(&client, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Set target address must succeed");

    const uint8_t payload[2] = {0x12, 0x34};
    uint8_t response[4] = {0};

    ret = nmbs_z_client_raw_pdu(&client, 0x41, payload, sizeof(payload), response,
                                sizeof(response), K_MSEC(100));
    zassert_equal(ret, 0, "Raw PDU exchange must succeed: %d", ret);
    zassert_equal(response[0], 0x12, "Raw response byte 0 mismatch: 0x%02X", response[0]);
    zassert_equal(response[1], 0x34, "Raw response byte 1 mismatch: 0x%02X", response[1]);
    zassert_equal(response[2], 0xAA, "Raw response byte 2 mismatch: 0x%02X", response[2]);
    zassert_equal(response[3], 0x55, "Raw response byte 3 mismatch: 0x%02X", response[3]);

    /* A raw exchange reports the raw error too. */
    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_ERROR_NONE,
                  "Raw PDU must clear last_error on success");
}

/* ========================================================================= */
/* Test 9: Thread-safe _addr client API variants                             */
/* ========================================================================= */
ZTEST(nanomodbus_client, test_client_addr_variants)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");

    const uint8_t target_unit = 7;
    uint16_t reg_val = 0;
    nmbs_bitfield bitfield_val = {0};

    /* 1. Read holding registers _addr */
    ret = nmbs_z_client_read_holding_registers_addr(&client, target_unit, 0, 1, &reg_val, K_MSEC(100));
    zassert_equal(ret, 0, "read_holding_registers_addr failed: %d", ret);

    /* 2. Read input registers _addr */
    ret = nmbs_z_client_read_input_registers_addr(&client, target_unit, 0, 1, &reg_val, K_MSEC(100));
    zassert_equal(ret, 0, "read_input_registers_addr failed: %d", ret);

    /* 3. Read coils _addr */
    ret = nmbs_z_client_read_coils_addr(&client, target_unit, 0, 1, bitfield_val, K_MSEC(100));
    zassert_equal(ret, 0, "read_coils_addr failed: %d", ret);

    /* 4. Read discrete inputs _addr */
    ret = nmbs_z_client_read_discrete_inputs_addr(&client, target_unit, 0, 1, bitfield_val, K_MSEC(100));
    zassert_equal(ret, 0, "read_discrete_inputs_addr failed: %d", ret);

    /* 5. Write single coil _addr */
    ret = nmbs_z_client_write_single_coil_addr(&client, target_unit, 0, true, K_MSEC(100));
    zassert_equal(ret, 0, "write_single_coil_addr failed: %d", ret);

    /* 6. Write single register _addr */
    ret = nmbs_z_client_write_single_register_addr(&client, target_unit, 0, 0x1234, K_MSEC(100));
    zassert_equal(ret, 0, "write_single_register_addr failed: %d", ret);

    /* 7. Write multiple coils _addr */
    ret = nmbs_z_client_write_multiple_coils_addr(&client, target_unit, 0, 1, bitfield_val, K_MSEC(100));
    zassert_equal(ret, 0, "write_multiple_coils_addr failed: %d", ret);

    /* 8. Write multiple registers _addr */
    uint16_t write_reg = 0x5678;
    ret = nmbs_z_client_write_multiple_registers_addr(&client, target_unit, 0, 1, &write_reg, K_MSEC(100));
    zassert_equal(ret, 0, "write_multiple_registers_addr failed: %d", ret);

    /* 9. Read/write registers _addr */
    ret = nmbs_z_client_read_write_registers_addr(&client, target_unit, 0, 1, &reg_val, 0, 1, &write_reg, K_MSEC(100));
    zassert_equal(ret, 0, "read_write_registers_addr failed: %d", ret);

    zassert_equal(nmbs_z_client_get_last_error(&client), NMBS_ERROR_NONE,
                  "last_error must be NMBS_ERROR_NONE after successful _addr calls");
}

/* ========================================================================= */
/* Test 10: Concurrent _addr calls with differing unit IDs                   */
/* ========================================================================= */
struct addr_worker_arg {
    nmbs_z_client_t *client;
    uint8_t unit_id;
    uint16_t reg_address;
    int iterations;
};

static K_THREAD_STACK_ARRAY_DEFINE(addr_worker_stacks, 2, 2048);
static struct k_thread addr_worker_threads[2];
static struct addr_worker_arg addr_args[2];
static atomic_t addr_worker_errors = ATOMIC_INIT(0);

static void addr_worker_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    struct addr_worker_arg *arg = (struct addr_worker_arg *)p1;

    for (int i = 0; i < arg->iterations; i++) {
        uint16_t val = 0;
        int ret = nmbs_z_client_read_holding_registers_addr(arg->client, arg->unit_id,
                                                            arg->reg_address, 1, &val,
                                                            K_MSEC(500));
        if (ret != 0) {
            atomic_inc(&addr_worker_errors);
        }
        k_yield();
    }
}

ZTEST(nanomodbus_client, test_client_addr_concurrency)
{
    nmbs_z_client_t client;
    nmbs_platform_conf platform;
    nmbs_platform_conf_create(&platform);
    platform.transport = NMBS_TRANSPORT_RTU;
    platform.read = mock_transport_read;
    platform.write = mock_transport_write;

    int ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init must succeed");

    atomic_set(&addr_worker_errors, 0);

    addr_args[0].client = &client;
    addr_args[0].unit_id = 10;
    addr_args[0].reg_address = 0;
    addr_args[0].iterations = 30;

    addr_args[1].client = &client;
    addr_args[1].unit_id = 20;
    addr_args[1].reg_address = 1;
    addr_args[1].iterations = 30;

    for (int t = 0; t < 2; t++) {
        k_thread_create(&addr_worker_threads[t],
                        addr_worker_stacks[t],
                        2048,
                        addr_worker_thread_fn,
                        &addr_args[t], NULL, NULL,
                        5 + t, 0, K_NO_WAIT);
    }

    for (int t = 0; t < 2; t++) {
        k_thread_join(&addr_worker_threads[t], K_FOREVER);
    }

    zassert_equal(atomic_get(&addr_worker_errors), 0,
                  "Concurrent _addr calls across different unit IDs must not error or clash");
}

ZTEST_SUITE(nanomodbus_client, NULL, NULL, test_client_setup, NULL, NULL);
