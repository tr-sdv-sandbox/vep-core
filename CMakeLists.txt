cmake_minimum_required(VERSION 3.16)
project(vep_core VERSION 0.1.0 LANGUAGES C CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
else()
    add_compile_options(-O2)
endif()

# ============================================================================
# Find required packages
# ============================================================================

find_package(glog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(Protobuf REQUIRED)
find_package(CycloneDDS REQUIRED)

# Abseil is required by protobuf when built with shared libs
find_package(absl QUIET)
if(absl_FOUND)
    message(STATUS "Found absl - will link with protobuf")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
pkg_check_modules(ZSTD REQUIRED libzstd)

# yaml-cpp compatibility: older versions use 'yaml-cpp' target, newer use 'yaml-cpp::yaml-cpp'
if(NOT TARGET yaml-cpp::yaml-cpp)
    if(TARGET yaml-cpp)
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    else()
        message(FATAL_ERROR "yaml-cpp target not found")
    endif()
endif()

# ============================================================================
# IDL from vep-schema (compiled at top-level CMakeLists.txt)
# ============================================================================
# vep_idl target is created at top-level and available to all components

if(NOT TARGET vep_idl)
    message(FATAL_ERROR "vep_idl target not found. Build from vehicle-edge-platform top-level.")
endif()

# ============================================================================
# Include vep-dds (provides vep_dds_common - IDL-agnostic DDS utilities)
# ============================================================================

if(NOT TARGET vep_dds_common)
    message(FATAL_ERROR "vep_dds_common target not found. Build from vehicle-edge-platform top-level or ensure vep-dds is built first.")
endif()

# ============================================================================
# libvssdag (REQUIRED for CAN-to-VSS transformation)
# ============================================================================

if(NOT TARGET vssdag)
    message(FATAL_ERROR "vssdag target not found. Build from vehicle-edge-platform top-level or ensure libvssdag is built first.")
endif()

# ============================================================================
# libvss-types (shared types)
# ============================================================================

if(NOT TARGET vss::types)
    message(FATAL_ERROR "vss::types target not found. Build from vehicle-edge-platform top-level or ensure libvss-types is built first.")
endif()

# ============================================================================
# libkuksa-cpp (Kuksa databroker client - optional)
# ============================================================================

if(TARGET kuksa)
    set(kuksa_FOUND TRUE)
else()
    message(WARNING "kuksa target not found - kuksa_dds_bridge will not be built")
    set(kuksa_FOUND FALSE)
endif()

# ============================================================================
# Compile transfer.proto
# ============================================================================

set(PROTO_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated_proto)
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

find_program(PROTOC_EXECUTABLE protoc REQUIRED)

set(TRANSFER_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/proto/transfer.proto)
set(TRANSFER_PROTO_SRC ${PROTO_OUTPUT_DIR}/transfer.pb.cc)
set(TRANSFER_PROTO_HDR ${PROTO_OUTPUT_DIR}/transfer.pb.h)

add_custom_command(
    OUTPUT ${TRANSFER_PROTO_SRC} ${TRANSFER_PROTO_HDR}
    COMMAND ${PROTOC_EXECUTABLE}
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
        --cpp_out=${PROTO_OUTPUT_DIR}
        ${TRANSFER_PROTO}
    DEPENDS ${TRANSFER_PROTO}
    COMMENT "Compiling transfer.proto"
)

add_library(transfer_proto STATIC ${TRANSFER_PROTO_SRC})
target_include_directories(transfer_proto PUBLIC ${PROTO_OUTPUT_DIR})
target_link_libraries(transfer_proto PUBLIC protobuf::libprotobuf)

# Link abseil if available (required by protobuf on some platforms)
if(absl_FOUND)
    target_link_libraries(transfer_proto PUBLIC
        absl::log_internal_check_op
        absl::log_internal_message
    )
endif()

# ============================================================================
# VEP Exporter (top-level application: DDS -> compressed MQTT)
# ============================================================================

# Exporter library (protobuf + zstd compressed MQTT sink)
add_library(vep_exporter_lib STATIC
    vep-exporter/src/compressed_mqtt_sink.cpp
    vep-exporter/src/dds_proto_conversion.cpp
    vep-exporter/src/subscriber.cpp
)

target_include_directories(vep_exporter_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vep-exporter/src
    ${PROTO_OUTPUT_DIR}
    ${MOSQUITTO_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
)

# Exporter links vep_idl which contains all message types
target_link_libraries(vep_exporter_lib PUBLIC
    vep_dds_common     # From vep-dds: DDS wrappers
    vep_idl            # All IDL types from vep-schema
    transfer_proto     # Our protobuf transfer types
    glog::glog
    ${MOSQUITTO_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

# Exporter executable
add_executable(vep_exporter
    vep-exporter/main.cpp
)

target_include_directories(vep_exporter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vep-exporter/src
)

target_link_libraries(vep_exporter PRIVATE
    vep_exporter_lib
    yaml-cpp::yaml-cpp
    glog::glog
)

# ============================================================================
# Verify all required components with CTest
# ============================================================================

enable_testing()

add_test(NAME check_cyclonedds COMMAND ${CMAKE_COMMAND} -E echo "CycloneDDS: OK")
add_test(NAME check_mosquitto COMMAND ${CMAKE_COMMAND} -E echo "libmosquitto: OK")
add_test(NAME check_zstd COMMAND ${CMAKE_COMMAND} -E echo "libzstd: OK")
add_test(NAME check_vssdag COMMAND ${CMAKE_COMMAND} -E echo "libvssdag: OK")
add_test(NAME check_vep_dds COMMAND ${CMAKE_COMMAND} -E echo "vep-dds: OK")

# ============================================================================
# Probes (each has its own IDL)
# ============================================================================

add_subdirectory(probes)

message(STATUS "Probes:")

# ============================================================================
# Tools (test utilities)
# ============================================================================

# vep_mqtt_receiver - receives MQTT messages, decodes and displays
add_executable(vep_mqtt_receiver
    tools/vep_mqtt_receiver/main.cpp
)

target_include_directories(vep_mqtt_receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROTO_OUTPUT_DIR}
    ${MOSQUITTO_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
)

target_link_libraries(vep_mqtt_receiver PRIVATE
    transfer_proto
    nlohmann_json::nlohmann_json
    glog::glog
    ${MOSQUITTO_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

# vep_host_metrics - host metrics collector
add_subdirectory(tools/vep_host_metrics)

# ============================================================================
# Kuksa-DDS Bridge (requires libkuksa-cpp)
# ============================================================================

if(kuksa_FOUND)
    # Bridge library
    add_library(kuksa_dds_bridge_lib STATIC
        src/bridge/vss_config.cpp
        src/bridge/kuksa_dds_bridge.cpp
        src/bridge/rt_transport.cpp
    )

    target_include_directories(kuksa_dds_bridge_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(kuksa_dds_bridge_lib PUBLIC
        kuksa                 # From libkuksa-cpp (alias for kuksa_cpp)
        vep_dds_common        # From vep-dds: DDS wrappers
        telemetry_idl         # Our IDL types
        nlohmann_json::nlohmann_json
        glog::glog
    )

    # Bridge executable
    add_executable(kuksa_dds_bridge
        src/bridge/main.cpp
    )

    target_link_libraries(kuksa_dds_bridge PRIVATE
        kuksa_dds_bridge_lib
        gflags
        glog::glog
    )

    # Test (--version exits 0 unlike --help which exits 1)
    add_test(NAME integration_kuksa_dds_bridge_runs
        COMMAND kuksa_dds_bridge --version
    )

    set(KUKSA_BRIDGE_ENABLED "Yes")
else()
    set(KUKSA_BRIDGE_ENABLED "No (libkuksa-cpp not found)")
endif()

# ============================================================================
# RT-DDS Bridge (DDS <-> RT transport)
# ============================================================================

# RT bridge library
add_library(rt_dds_bridge_lib STATIC
    src/bridge/rt_transport.cpp
    src/rt_bridge/rt_dds_bridge.cpp
)

target_include_directories(rt_dds_bridge_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rt_dds_bridge_lib PUBLIC
    vep_dds_common        # From vep-dds: DDS wrappers
    telemetry_idl         # Our IDL types
    glog::glog
)

# RT bridge executable
add_executable(rt_dds_bridge
    src/rt_bridge/main.cpp
)

target_link_libraries(rt_dds_bridge PRIVATE
    rt_dds_bridge_lib
    gflags
    glog::glog
)

# Test
add_test(NAME integration_rt_dds_bridge_runs
    COMMAND rt_dds_bridge --version
)

# ============================================================================
# Integration tests
# ============================================================================

add_test(NAME integration_exporter_runs
    COMMAND vep_exporter --help
)

add_test(NAME integration_vep_mqtt_receiver_runs
    COMMAND vep_mqtt_receiver --help
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== VEP-Core Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "CycloneDDS: ${CycloneDDS_VERSION}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  vep-dds: vep_dds_common (DDS utilities)")
message(STATUS "  libvssdag: ${vssdag_FOUND}")
message(STATUS "  libkuksa-cpp: ${kuksa_FOUND}")
message(STATUS "  zstd: ${ZSTD_VERSION}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  Libraries:")
message(STATUS "    - vep_exporter_lib      (protobuf + zstd compressed MQTT sink)")
if(kuksa_FOUND)
message(STATUS "    - kuksa_dds_bridge_lib  (Kuksa <-> DDS bridge)")
endif()
message(STATUS "    - rt_dds_bridge_lib     (DDS <-> RT transport bridge)")
message(STATUS "  Probes (in probes/ subdirectory):")
if(TARGET vep_can_probe)
message(STATUS "    - vep_can_probe         (CAN -> VSS -> DDS)")
endif()
if(TARGET vep_otel_probe)
message(STATUS "    - vep_otel_probe        (OTLP gRPC -> DDS)")
endif()
if(TARGET vep_avtp_probe)
message(STATUS "    - vep_avtp_probe        (IEEE 1722 AVTP -> DDS)")
endif()
message(STATUS "  Bridges/Exporters:")
message(STATUS "    - vep_exporter          (DDS subscriber -> compressed MQTT)")
if(kuksa_FOUND)
message(STATUS "    - kuksa_dds_bridge      (Kuksa databroker <-> DDS bridge)")
endif()
message(STATUS "    - rt_dds_bridge         (DDS <-> RT transport bridge)")
message(STATUS "  Tools:")
message(STATUS "    - vep_mqtt_receiver     (MQTT -> decompress -> display)")
message(STATUS "    - vep_host_metrics      (Linux host metrics -> OTLP gRPC)")
message(STATUS "")
message(STATUS "Kuksa-DDS Bridge: ${KUKSA_BRIDGE_ENABLED}")
message(STATUS "")
message(STATUS "Run 'ctest' after build to verify all components")
message(STATUS "")
