cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_nanomodbus_baseline)

target_sources(app PRIVATE
    src/test_build_baseline.c
    src/test_error_mapping.c
    src/test_rs485_timing.c
    src/test_client_concurrency.c
    src/test_server_rtu.c
    src/test_server_tcp.c
    src/test_multi_instance.c
    src/test_nanomodbus_serial.c
    src/test_shell.c
)
