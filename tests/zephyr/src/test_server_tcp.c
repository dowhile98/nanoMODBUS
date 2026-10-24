/*
 * Copyright (c) 2026 nanoMODBUS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>
#include <zephyr/net/socket.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>

static uint16_t server_holding_registers[16];

static nmbs_error mock_read_holding_registers(uint16_t address, uint16_t quantity,
                                             uint16_t *registers_out, uint8_t unit_id,
                                             void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (address + quantity > ARRAY_SIZE(server_holding_registers)) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers_out[i] = server_holding_registers[address + i];
    }

    return NMBS_ERROR_NONE;
}

static nmbs_error mock_write_single_register(uint16_t address, uint16_t value,
                                            uint8_t unit_id, void *arg)
{
    ARG_UNUSED(unit_id);
    ARG_UNUSED(arg);

    if (address >= ARRAY_SIZE(server_holding_registers)) {
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    }

    server_holding_registers[address] = value;
    return NMBS_ERROR_NONE;
}

static void server_tcp_before(void *fixture)
{
    ARG_UNUSED(fixture);
    for (int i = 0; i < ARRAY_SIZE(server_holding_registers); i++) {
        server_holding_registers[i] = 100 + i;
    }
}

/* Test 1: Full round-trip read and write over Modbus TCP */
ZTEST(nanomodbus_server_tcp, test_tcp_server_client_roundtrip)
{
    nmbs_z_server_tcp_t server;
    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;
    cbs.write_single_register = mock_write_single_register;

    int ret = nmbs_z_server_tcp_start(&server, &cbs, 5020);
    zassert_equal(ret, 0, "Server TCP start on 5020 failed: %d", ret);

    nmbs_z_tcp_transport_t transport;
    nmbs_platform_conf platform;
    ret = nmbs_z_tcp_transport_connect(&transport, "127.0.0.1", 5020, 1000, &platform);
    zassert_equal(ret, 0, "Client connect failed: %d", ret);

    nmbs_z_client_t client;
    ret = nmbs_z_client_init(&client, &platform);
    zassert_equal(ret, 0, "Client init failed: %d", ret);

    /* FC 03: Read holding registers */
    uint16_t read_regs[2] = {0};
    ret = nmbs_z_client_read_holding_registers(&client, 0, 2, read_regs, K_MSEC(1000));
    zassert_equal(ret, 0, "Read holding registers failed: %d", ret);
    zassert_equal(read_regs[0], 100, "Expected reg 0 = 100, got %d", read_regs[0]);
    zassert_equal(read_regs[1], 101, "Expected reg 1 = 101, got %d", read_regs[1]);

    /* FC 06: Write single register */
    ret = nmbs_z_client_write_single_register(&client, 0, 0xABCD, K_MSEC(1000));
    zassert_equal(ret, 0, "Write single register failed: %d", ret);
    zassert_equal(server_holding_registers[0], 0xABCD, "Server register not updated");

    /* Verify write with another read */
    ret = nmbs_z_client_read_holding_registers(&client, 0, 1, read_regs, K_MSEC(1000));
    zassert_equal(ret, 0, "Second read failed: %d", ret);
    zassert_equal(read_regs[0], 0xABCD, "Expected reg 0 = 0xABCD");

    ret = nmbs_z_tcp_transport_close(&transport);
    zassert_equal(ret, 0, "Transport close failed");

    ret = nmbs_z_server_tcp_stop(&server);
    zassert_equal(ret, 0, "Server stop failed: %d", ret);
}

/* Test 2 (Review Focus 4): Abrupt client abort mid-stream and clean server recovery */
ZTEST(nanomodbus_server_tcp, test_tcp_server_client_abort_recovery)
{
    nmbs_z_server_tcp_t server;
    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_tcp_start(&server, &cbs, 5021);
    zassert_equal(ret, 0, "Server TCP start on 5021 failed: %d", ret);

    /* Client 1 connects, executes 1 transaction, then abruptly closes socket */
    nmbs_z_tcp_transport_t t1;
    nmbs_platform_conf p1;
    ret = nmbs_z_tcp_transport_connect(&t1, "127.0.0.1", 5021, 1000, &p1);
    zassert_equal(ret, 0, "Client 1 connect failed: %d", ret);

    nmbs_z_client_t c1;
    ret = nmbs_z_client_init(&c1, &p1);
    zassert_equal(ret, 0, "Client 1 init failed: %d", ret);

    uint16_t val = 0;
    ret = nmbs_z_client_read_holding_registers(&c1, 0, 1, &val, K_MSEC(1000));
    zassert_equal(ret, 0, "Client 1 read failed: %d", ret);
    zassert_equal(val, 100, "Unexpected reg value: %d", val);

    /* Abruptly terminate client 1 socket to simulate link drop / reset mid-stream */
    zsock_close(t1.sock);
    t1.sock = -1;

    /* Allow server thread state machine to detect disconnection and return to listening */
    k_sleep(K_MSEC(100));

    /* Verify server is still alive and accepting subsequent connections */
    nmbs_z_tcp_transport_t t2;
    nmbs_platform_conf p2;
    ret = nmbs_z_tcp_transport_connect(&t2, "127.0.0.1", 5021, 1000, &p2);
    zassert_equal(ret, 0, "Client 2 connect failed after previous abort: %d", ret);

    nmbs_z_client_t c2;
    ret = nmbs_z_client_init(&c2, &p2);
    zassert_equal(ret, 0, "Client 2 init failed: %d", ret);

    val = 0;
    ret = nmbs_z_client_read_holding_registers(&c2, 1, 1, &val, K_MSEC(1000));
    zassert_equal(ret, 0, "Client 2 read failed after abort recovery: %d", ret);
    zassert_equal(val, 101, "Unexpected reg value for client 2: %d", val);

    nmbs_z_tcp_transport_close(&t2);
    ret = nmbs_z_server_tcp_stop(&server);
    zassert_equal(ret, 0, "Server TCP stop failed: %d", ret);
}

/* Test 3: Graceful server shutdown */
ZTEST(nanomodbus_server_tcp, test_tcp_server_graceful_shutdown)
{
    nmbs_z_server_tcp_t server;
    nmbs_callbacks cbs = {0};
    cbs.read_holding_registers = mock_read_holding_registers;

    int ret = nmbs_z_server_tcp_start(&server, &cbs, 5022);
    zassert_equal(ret, 0, "Server start failed: %d", ret);
    zassert_equal(atomic_get(&server.running), 1, "Server should be running");

    ret = nmbs_z_server_tcp_stop(&server);
    zassert_equal(ret, 0, "Server stop failed: %d", ret);
    zassert_equal(atomic_get(&server.running), 0, "Server should be stopped");

    /* Idempotent stop check */
    ret = nmbs_z_server_tcp_stop(&server);
    zassert_equal(ret, 0, "Second server stop should succeed idempotently");
}

ZTEST_SUITE(nanomodbus_server_tcp, NULL, NULL, server_tcp_before, NULL, NULL);
