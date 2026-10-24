# SPDX-License-Identifier: MIT

if(CONFIG_NANOMODBUS)
  zephyr_library()

  # Public include directories exposed to application targets
  zephyr_include_directories(include)
  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

  # Internal include directory for nanomodbus.h core header
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

  # Always include the core protocol engine
  zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/../nanomodbus.c)
  zephyr_library_sources(src/nanomodbus_z_common.c)

  # Role implementations
  zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_CLIENT src/nanomodbus_z_client.c)
  zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_SERVER_RTU src/nanomodbus_z_server_rtu.c)
  zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_SERVER_TCP src/nanomodbus_z_server_tcp.c)

  # Transport adapters
  zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_TRANSPORT_SERIAL src/nanomodbus_z_transport_serial.c)
  zephyr_library_sources_ifdef(CONFIG_NANOMODBUS_TRANSPORT_TCP src/nanomodbus_z_transport_tcp.c)

  # Role Disabling
  if(NOT CONFIG_NANOMODBUS_SERVER)
    zephyr_compile_definitions(NMBS_SERVER_DISABLED)
  endif()

  if(NOT CONFIG_NANOMODBUS_CLIENT)
    zephyr_compile_definitions(NMBS_CLIENT_DISABLED)
  endif()

  # Granular Function Code Disables (Client & Server)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_COILS_DISABLED
    NMBS_SERVER_READ_COILS_DISABLED NMBS_CLIENT_READ_COILS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_DISCRETE_INPUTS_DISABLED
    NMBS_SERVER_READ_DISCRETE_INPUTS_DISABLED NMBS_CLIENT_READ_DISCRETE_INPUTS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_HOLDING_REGISTERS_DISABLED
    NMBS_SERVER_READ_HOLDING_REGISTERS_DISABLED NMBS_CLIENT_READ_HOLDING_REGISTERS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_INPUT_REGISTERS_DISABLED
    NMBS_SERVER_READ_INPUT_REGISTERS_DISABLED NMBS_CLIENT_READ_INPUT_REGISTERS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_WRITE_SINGLE_COIL_DISABLED
    NMBS_SERVER_WRITE_SINGLE_COIL_DISABLED NMBS_CLIENT_WRITE_SINGLE_COIL_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_WRITE_SINGLE_REGISTER_DISABLED
    NMBS_SERVER_WRITE_SINGLE_REGISTER_DISABLED NMBS_CLIENT_WRITE_SINGLE_REGISTER_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_WRITE_MULTIPLE_COILS_DISABLED
    NMBS_SERVER_WRITE_MULTIPLE_COILS_DISABLED NMBS_CLIENT_WRITE_MULTIPLE_COILS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_WRITE_MULTIPLE_REGISTERS_DISABLED
    NMBS_SERVER_WRITE_MULTIPLE_REGISTERS_DISABLED NMBS_CLIENT_WRITE_MULTIPLE_REGISTERS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_FILE_RECORD_DISABLED
    NMBS_SERVER_READ_FILE_RECORD_DISABLED NMBS_CLIENT_READ_FILE_RECORD_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_WRITE_FILE_RECORD_DISABLED
    NMBS_SERVER_WRITE_FILE_RECORD_DISABLED NMBS_CLIENT_WRITE_FILE_RECORD_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_WRITE_REGISTERS_DISABLED
    NMBS_SERVER_READ_WRITE_REGISTERS_DISABLED NMBS_CLIENT_READ_WRITE_REGISTERS_DISABLED)
  zephyr_compile_definitions_ifdef(CONFIG_NANOMODBUS_FC_READ_DEVICE_IDENTIFICATION_DISABLED
    NMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED NMBS_CLIENT_READ_DEVICE_IDENTIFICATION_DISABLED)

  # Tuning & Configuration
  if(DEFINED CONFIG_NANOMODBUS_BITFIELD_MAX)
    zephyr_compile_definitions(NMBS_BITFIELD_MAX=${CONFIG_NANOMODBUS_BITFIELD_MAX})
  endif()

  if(CONFIG_NANOMODBUS_DEBUG)
    zephyr_compile_definitions(NMBS_DEBUG)
  endif()
endif()
