/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <string.h>

#define MULTI_INST_REGS_COUNT 16
#define NUM_OPS_PER_THREAD 25
#define TCP_PORT_MULTI 5025

/* Mock RTU Bus context for an isolated client */
typedef struct {
    struct k_mutex lock;
    uint8_t rx_buf[256];
    uint16_t rx_len;
    uint16_t rx_pos;
    uint16_t holding_regs[MULTI_INST_REGS_COUNT];
    uint32_t ops_count;
} mock_rtu_bus_t;

static void mock_bus_append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = nmbs_crc_calc(buf, len, NULL);
    buf[len] = (uint8_t)((crc >> 8) & 0xFF);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
}

static int32_t mock_bus_read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);
    mock_rtu_bus_t *bus = (mock_rtu_bus_t *)arg;

    k_mutex_lock(&bus->lock, K_FOREVER);
    if (bus->rx_pos + count > bus->rx_len) {
        k_mutex_unlock(&bus->lock);
        return 0;
    }

    memcpy(buf, &bus->rx_buf[bus->rx_pos], count);
    bus->rx_pos += count;
    k_mutex_unlock(&bus->lock);
    return count;
}

static int32_t mock_bus_write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    ARG_UNUSED(byte_timeout_ms);
    mock_rtu_bus_t *bus = (mock_rtu_bus_t *)arg;

    if (count < 4) {
        return count;
    }

    k_mutex_lock(&bus->lock, K_FOREVER);
    uint8_t unit_id = buf[0];
    uint8_t fc = buf[1];
    bus->rx_pos = 0;
    bus->rx_len = 0;

    if (fc == 0x03) { /* Read Holding Registers */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t qty = ((uint16_t)buf[4] << 8) | buf[5];
        uint8_t bytes = (uint8_t)(qty * 2);

        bus->rx_buf[0] = unit_id;
        bus->rx_buf[1] = 0x03;
        bus->rx_buf[2] = bytes;
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t reg_val = 0;
            if (addr + i < MULTI_INST_REGS_COUNT) {
                reg_val = bus->holding_regs[addr + i];
            }
            bus->rx_buf[3 + 2 * i] = (uint8_t)((reg_val >> 8) & 0xFF);
            bus->rx_buf[4 + 2 * i] = (uint8_t)(reg_val & 0xFF);
        }
        mock_bus_append_crc(bus->rx_buf, 3 + bytes);
        bus->rx_len = 3 + bytes + 2;
        bus->ops_count++;
    } else if (fc == 0x06) { /* Write Single Register */
        uint16_t addr = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t val = ((uint16_t)buf[4] << 8) | buf[5];
        if (addr < MULTI_INST_REGS_COUNT) {
            bus->holding_regs[addr] = val;
        }
        memcpy(bus->rx_buf, buf, 6);
        mock_bus_append_crc(bus->rx_buf, 6);
        bus->rx_len = 8;
        bus->ops_count++;
    }

    k_mutex_unlock(&bus->lock);
    k_yield();
    return count;
}

/* Modbus TCP Server state and callbacks */
static uint16_t tcp_server_holding_regs[MULTI_INST_REGS_COUNT];
static uint32_t tcp_server_ops_count;

static nmbs_error server_tcp_read_holding_registers(uint16_t address, uint16_t quantity,
                                                   uint16_t *registers_out, uint8_t unit_id,
                                                   void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (address + quantity > MULTI_INST_REGS_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = tcp_server_holding_regs[address + i];
    }
    tcp_server_ops_count++;
    return NMBS_ERROR_NONE;
}

static nmbs_error server_tcp_write_single_register(uint16_t address, uint16_t value,
                                                  uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (address >= MULTI_INST_REGS_COUNT) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    tcp_server_holding_regs[address] = value;
    tcp_server_ops_count++;
    return NMBS_ERROR_NONE;
}

/* Multi-instance test contexts */
static mock_rtu_bus_t bus1;
static mock_rtu_bus_t bus2;
static nmbs_z_client_t client1;
static nmbs_z_client_t client2;
static nmbs_z_client_t client3;
static nmbs_z_tcp_transport_t transport3;
static nmbs_z_server_tcp_t server_tcp;

static struct k_sem start_sem;
static uint32_t thread_a_success;
static uint32_t thread_b_success;
static uint32_t thread_c_success;
static uint32_t thread_a_errors;
static uint32_t thread_b_errors;
static uint32_t thread_c_errors;

static K_THREAD_STACK_DEFINE(stack_a, 2048);
static K_THREAD_STACK_DEFINE(stack_b, 2048);
static K_THREAD_STACK_DEFINE(stack_c, 2048);
static struct k_thread thread_a;
static struct k_thread thread_b;
static struct k_thread thread_c;

static void thread_a_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    k_sem_take(&start_sem, K_FOREVER);

    for (int i = 0; i < NUM_OPS_PER_THREAD; i++) {
        uint16_t regs[2] = {0};
        int ret = nmbs_z_client_read_holding_registers(&client1, 0, 2, regs, K_MSEC(500));
        if (ret != 0) {
            thread_a_errors++;
            continue;
        }

        ret = nmbs_z_client_write_single_register(&client1, 0, (uint16_t)(1000 + i), K_MSEC(500));
        if (ret != 0) {
            thread_a_errors++;
            continue;
        }

        ret = nmbs_z_client_read_holding_registers(&client1, 0, 1, regs, K_MSEC(500));
        if (ret != 0 || regs[0] != (uint16_t)(1000 + i)) {
            thread_a_errors++;
            continue;
        }

        thread_a_success++;
        k_sleep(K_MSEC(1));
    }
}

static void thread_b_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    k_sem_take(&start_sem, K_FOREVER);

    for (int i = 0; i < NUM_OPS_PER_THREAD; i++) {
        uint16_t regs[2] = {0};
        int ret = nmbs_z_client_read_holding_registers(&client2, 0, 2, regs, K_MSEC(500));
        if (ret != 0) {
            thread_b_errors++;
            continue;
        }

        ret = nmbs_z_client_write_single_register(&client2, 0, (uint16_t)(2000 + i), K_MSEC(500));
        if (ret != 0) {
            thread_b_errors++;
            continue;
        }

        ret = nmbs_z_client_read_holding_registers(&client2, 0, 1, regs, K_MSEC(500));
        if (ret != 0 || regs[0] != (uint16_t)(2000 + i)) {
            thread_b_errors++;
            continue;
        }

        thread_b_success++;
        k_sleep(K_MSEC(1));
    }
}

static void thread_c_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    k_sem_take(&start_sem, K_FOREVER);

    for (int i = 0; i < NUM_OPS_PER_THREAD; i++) {
        uint16_t regs[2] = {0};
        int ret = nmbs_z_client_read_holding_registers(&client3, 0, 2, regs, K_MSEC(1000));
        if (ret != 0) {
            thread_c_errors++;
            continue;
        }

        ret = nmbs_z_client_write_single_register(&client3, 0, (uint16_t)(5000 + i), K_MSEC(1000));
        if (ret != 0) {
            thread_c_errors++;
            continue;
        }

        ret = nmbs_z_client_read_holding_registers(&client3, 0, 1, regs, K_MSEC(1000));
        if (ret != 0 || regs[0] != (uint16_t)(5000 + i)) {
            thread_c_errors++;
            continue;
        }

        thread_c_success++;
        k_sleep(K_MSEC(1));
    }
}

/* Concurrency & Multi-Instance Unit Test (Review Focus 5) */
ZTEST(nanomodbus_multi_instance, test_concurrent_multi_instances)
{
    /* 1. Initialize Bus 1 and Client 1 */
    memset(&bus1, 0, sizeof(bus1));
    k_mutex_init(&bus1.lock);
    for (int i = 0; i < MULTI_INST_REGS_COUNT; i++) {
        bus1.holding_regs[i] = (uint16_t)(1000 + i);
    }

    nmbs_platform_conf plat1;
    nmbs_platform_conf_create(&plat1);
    plat1.transport = NMBS_TRANSPORT_RTU;
    plat1.read = mock_bus_read;
    plat1.write = mock_bus_write;
    plat1.arg = &bus1;

    int ret = nmbs_z_client_init(&client1, &plat1);
    zassert_equal(ret, 0, "Client 1 init failed: %d", ret);
    ret = nmbs_z_client_set_target_address(&client1, 1, K_MSEC(100));
    zassert_equal(ret, 0, "Client 1 set target address failed: %d", ret);

    /* 2. Initialize Bus 2 and Client 2 */
    memset(&bus2, 0, sizeof(bus2));
    k_mutex_init(&bus2.lock);
    for (int i = 0; i < MULTI_INST_REGS_COUNT; i++) {
        bus2.holding_regs[i] = (uint16_t)(2000 + i);
    }

    nmbs_platform_conf plat2;
    nmbs_platform_conf_create(&plat2);
    plat2.transport = NMBS_TRANSPORT_RTU;
    plat2.read = mock_bus_read;
    plat2.write = mock_bus_write;
    plat2.arg = &bus2;

    ret = nmbs_z_client_init(&client2, &plat2);
    zassert_equal(ret, 0, "Client 2 init failed: %d", ret);
    ret = nmbs_z_client_set_target_address(&client2, 2, K_MSEC(100));
    zassert_equal(ret, 0, "Client 2 set target address failed: %d", ret);

    /* 3. Initialize TCP Server on loopback port 5025 */
    memset(tcp_server_holding_regs, 0, sizeof(tcp_server_holding_regs));
    for (int i = 0; i < MULTI_INST_REGS_COUNT; i++) {
        tcp_server_holding_regs[i] = (uint16_t)(5000 + i);
    }
    tcp_server_ops_count = 0;

    nmbs_callbacks tcp_cbs = {0};
    tcp_cbs.read_holding_registers = server_tcp_read_holding_registers;
    tcp_cbs.write_single_register = server_tcp_write_single_register;

    ret = nmbs_z_server_tcp_start(&server_tcp, &tcp_cbs, TCP_PORT_MULTI);
    zassert_equal(ret, 0, "TCP Server start on %d failed: %d", TCP_PORT_MULTI, ret);

    /* 4. Connect TCP Client 3 to loopback server */
    nmbs_platform_conf plat3;
    ret = nmbs_z_tcp_transport_connect(&transport3, "127.0.0.1", TCP_PORT_MULTI, 2000, &plat3);
    zassert_equal(ret, 0, "TCP Client 3 connect failed: %d", ret);

    ret = nmbs_z_client_init(&client3, &plat3);
    zassert_equal(ret, 0, "Client 3 init failed: %d", ret);

    /* 5. Start all 3 threads simultaneously */
    thread_a_success = 0;
    thread_b_success = 0;
    thread_c_success = 0;
    thread_a_errors = 0;
    thread_b_errors = 0;
    thread_c_errors = 0;

    k_sem_init(&start_sem, 0, 3);

    k_thread_create(&thread_a, stack_a, K_THREAD_STACK_SIZEOF(stack_a),
                    thread_a_fn, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);
    k_thread_create(&thread_b, stack_b, K_THREAD_STACK_SIZEOF(stack_b),
                    thread_b_fn, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);
    k_thread_create(&thread_c, stack_c, K_THREAD_STACK_SIZEOF(stack_c),
                    thread_c_fn, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    /* Release all threads to run concurrently */
    k_sem_give(&start_sem);
    k_sem_give(&start_sem);
    k_sem_give(&start_sem);

    /* Wait for all 3 threads to complete */
    k_thread_join(&thread_a, K_FOREVER);
    k_thread_join(&thread_b, K_FOREVER);
    k_thread_join(&thread_c, K_FOREVER);

    /* 6. Assertions: 100% success across all 3 instances */
    zassert_equal(thread_a_errors, 0, "Thread A had %u errors", thread_a_errors);
    zassert_equal(thread_a_success, NUM_OPS_PER_THREAD, "Thread A completed %u/%u ops",
                  thread_a_success, NUM_OPS_PER_THREAD);

    zassert_equal(thread_b_errors, 0, "Thread B had %u errors", thread_b_errors);
    zassert_equal(thread_b_success, NUM_OPS_PER_THREAD, "Thread B completed %u/%u ops",
                  thread_b_success, NUM_OPS_PER_THREAD);

    zassert_equal(thread_c_errors, 0, "Thread C had %u errors", thread_c_errors);
    zassert_equal(thread_c_success, NUM_OPS_PER_THREAD, "Thread C completed %u/%u ops",
                  thread_c_success, NUM_OPS_PER_THREAD);

    /* 7. Zero memory corruption and zero cross-contamination */
    /* Bus 1 holding registers: reg 0 written to 1000 + NUM_OPS - 1, regs 1..15 untouched */
    zassert_equal(bus1.holding_regs[0], (uint16_t)(1000 + NUM_OPS_PER_THREAD - 1),
                  "Bus 1 corrupted: reg 0 is %u", bus1.holding_regs[0]);
    for (int i = 1; i < MULTI_INST_REGS_COUNT; i++) {
        zassert_equal(bus1.holding_regs[i], (uint16_t)(1000 + i),
                      "Bus 1 corrupted: reg %d is %u", i, bus1.holding_regs[i]);
    }

    /* Bus 2 holding registers: reg 0 written to 2000 + NUM_OPS - 1, regs 1..15 untouched */
    zassert_equal(bus2.holding_regs[0], (uint16_t)(2000 + NUM_OPS_PER_THREAD - 1),
                  "Bus 2 corrupted: reg 0 is %u", bus2.holding_regs[0]);
    for (int i = 1; i < MULTI_INST_REGS_COUNT; i++) {
        zassert_equal(bus2.holding_regs[i], (uint16_t)(2000 + i),
                      "Bus 2 corrupted: reg %d is %u", i, bus2.holding_regs[i]);
    }

    /* TCP Server registers: reg 0 written to 5000 + NUM_OPS - 1, regs 1..15 untouched */
    zassert_equal(tcp_server_holding_regs[0], (uint16_t)(5000 + NUM_OPS_PER_THREAD - 1),
                  "TCP Server corrupted: reg 0 is %u", tcp_server_holding_regs[0]);
    for (int i = 1; i < MULTI_INST_REGS_COUNT; i++) {
        zassert_equal(tcp_server_holding_regs[i], (uint16_t)(5000 + i),
                      "TCP Server corrupted: reg %d is %u", i, tcp_server_holding_regs[i]);
    }

    /* Verify independent mutex states */
    zassert_equal(k_mutex_lock(&client1.lock, K_NO_WAIT), 0, "Client 1 lock not free");
    k_mutex_unlock(&client1.lock);

    zassert_equal(k_mutex_lock(&client2.lock, K_NO_WAIT), 0, "Client 2 lock not free");
    k_mutex_unlock(&client2.lock);

    zassert_equal(k_mutex_lock(&client3.lock, K_NO_WAIT), 0, "Client 3 lock not free");
    k_mutex_unlock(&client3.lock);

    /* 8. Clean shutdown */
    ret = nmbs_z_tcp_transport_close(&transport3);
    zassert_equal(ret, 0, "Transport 3 close failed: %d", ret);

    ret = nmbs_z_server_tcp_stop(&server_tcp);
    zassert_equal(ret, 0, "Server TCP stop failed: %d", ret);
}

ZTEST_SUITE(nanomodbus_multi_instance, NULL, NULL, NULL, NULL, NULL);
