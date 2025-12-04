cmake_minimum_required(VERSION 3.16)
project(covesa_ifex_vdr_integration VERSION 0.1.0 LANGUAGES C CXX)

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
pkg_check_modules(ZSTD REQUIRED libzstd)

# ============================================================================
# Compile our telemetry IDL (imports types from libvss-types)
# ============================================================================

find_program(IDLC_EXECUTABLE idlc REQUIRED)

set(IDL_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated_idl)
file(MAKE_DIRECTORY ${IDL_OUTPUT_DIR})

# Paths to libvss-types IDL files
set(LIBVSS_TYPES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libvss-types")
set(LIBVSS_TYPES_IDL_DIR "${LIBVSS_TYPES_DIR}/idl")

set(TELEMETRY_IDL ${CMAKE_CURRENT_SOURCE_DIR}/idl/telemetry.idl)
set(VSS_TYPES_IDL ${LIBVSS_TYPES_IDL_DIR}/vss_types.idl)
set(VSS_SIGNAL_IDL ${LIBVSS_TYPES_IDL_DIR}/vss_signal.idl)

# Generated files
set(VSS_TYPES_IDL_SRC ${IDL_OUTPUT_DIR}/vss_types.c)
set(VSS_TYPES_IDL_HDR ${IDL_OUTPUT_DIR}/vss_types.h)
set(VSS_SIGNAL_IDL_SRC ${IDL_OUTPUT_DIR}/vss_signal.c)
set(VSS_SIGNAL_IDL_HDR ${IDL_OUTPUT_DIR}/vss_signal.h)
set(TELEMETRY_IDL_SRC ${IDL_OUTPUT_DIR}/telemetry.c)
set(TELEMETRY_IDL_HDR ${IDL_OUTPUT_DIR}/telemetry.h)

# Step 1: Compile vss_types.idl (no dependencies)
add_custom_command(
    OUTPUT ${VSS_TYPES_IDL_SRC} ${VSS_TYPES_IDL_HDR}
    COMMAND ${IDLC_EXECUTABLE} -l c ${VSS_TYPES_IDL}
    WORKING_DIRECTORY ${IDL_OUTPUT_DIR}
    DEPENDS ${VSS_TYPES_IDL}
    COMMENT "Compiling vss_types.idl"
)

# Step 2: Compile vss_signal.idl (depends on vss_types)
add_custom_command(
    OUTPUT ${VSS_SIGNAL_IDL_SRC} ${VSS_SIGNAL_IDL_HDR}
    COMMAND ${IDLC_EXECUTABLE} -l c -I${LIBVSS_TYPES_IDL_DIR} ${VSS_SIGNAL_IDL}
    WORKING_DIRECTORY ${IDL_OUTPUT_DIR}
    DEPENDS ${VSS_SIGNAL_IDL} ${VSS_TYPES_IDL_HDR}
    COMMENT "Compiling vss_signal.idl"
)

# Step 3: Compile telemetry.idl (depends on vss_types and vss_signal)
add_custom_command(
    OUTPUT ${TELEMETRY_IDL_SRC} ${TELEMETRY_IDL_HDR}
    COMMAND ${IDLC_EXECUTABLE} -l c -I${LIBVSS_TYPES_IDL_DIR} ${TELEMETRY_IDL}
    WORKING_DIRECTORY ${IDL_OUTPUT_DIR}
    DEPENDS ${TELEMETRY_IDL} ${VSS_TYPES_IDL_HDR} ${VSS_SIGNAL_IDL_HDR}
    COMMENT "Compiling telemetry.idl (with libvss-types includes)"
)

add_library(telemetry_idl STATIC
    ${VSS_TYPES_IDL_SRC}
    ${VSS_SIGNAL_IDL_SRC}
    ${TELEMETRY_IDL_SRC}
)
set_target_properties(telemetry_idl PROPERTIES LINKER_LANGUAGE C)
target_include_directories(telemetry_idl PUBLIC ${IDL_OUTPUT_DIR})
target_link_libraries(telemetry_idl PUBLIC CycloneDDS::ddsc)

add_custom_target(generate_idl DEPENDS
    ${VSS_TYPES_IDL_SRC} ${VSS_TYPES_IDL_HDR}
    ${VSS_SIGNAL_IDL_SRC} ${VSS_SIGNAL_IDL_HDR}
    ${TELEMETRY_IDL_SRC} ${TELEMETRY_IDL_HDR}
)
add_dependencies(telemetry_idl generate_idl)

# ============================================================================
# Include vdr-light (provides vdr_common - IDL-agnostic DDS utilities)
# ============================================================================

set(VDR_LIGHT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vdr-light")
if(NOT EXISTS "${VDR_LIGHT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "vdr-light not found at ${VDR_LIGHT_DIR}")
endif()

# Don't build vdr-light examples - we have our own IDL
set(VDR_LIGHT_BUILD_EXAMPLES OFF CACHE BOOL "Skip vdr-light examples" FORCE)

add_subdirectory(${VDR_LIGHT_DIR} ${CMAKE_BINARY_DIR}/vdr-light)

# ============================================================================
# libvssdag (REQUIRED for CAN-to-VSS transformation)
# ============================================================================

find_package(vssdag QUIET)
if(NOT vssdag_FOUND)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libvssdag/CMakeLists.txt")
        message(STATUS "Found libvssdag in parent directory")
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(../libvssdag ${CMAKE_BINARY_DIR}/libvssdag)
        set(vssdag_FOUND TRUE)
    else()
        message(FATAL_ERROR "libvssdag not found. Please install it or place it in ../libvssdag")
    endif()
endif()

# ============================================================================
# libvss-types (shared types - may already be included by dependencies)
# ============================================================================

if(NOT TARGET vss::types)
    find_package(vss-types QUIET)
    if(NOT vss-types_FOUND)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libvss-types/CMakeLists.txt")
            message(STATUS "Found libvss-types in parent directory")
            add_subdirectory(../libvss-types ${CMAKE_BINARY_DIR}/libvss-types)
        else()
            message(FATAL_ERROR "libvss-types not found")
        endif()
    endif()
endif()

# ============================================================================
# libkuksa-cpp (Kuksa databroker client)
# ============================================================================

find_package(kuksa QUIET)
if(NOT kuksa_FOUND)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libkuksa-cpp/CMakeLists.txt")
        message(STATUS "Found libkuksa-cpp in parent directory")
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(../libkuksa-cpp ${CMAKE_BINARY_DIR}/libkuksa-cpp)
        set(kuksa_FOUND TRUE)
    else()
        message(WARNING "libkuksa-cpp not found - kuksa_dds_bridge will not be built")
        set(kuksa_FOUND FALSE)
    endif()
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

# ============================================================================
# Exporter library (protobuf + zstd compressed MQTT sink)
# ============================================================================

add_library(vdr_exporter_lib STATIC
    src/exporter/compressed_mqtt_sink.cpp
    src/exporter/dds_proto_conversion.cpp
    src/exporter/subscriber.cpp
)

target_include_directories(vdr_exporter_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROTO_OUTPUT_DIR}
    ${MOSQUITTO_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
)

target_link_libraries(vdr_exporter_lib PUBLIC
    vdr_common         # From vdr-light: DDS wrappers
    telemetry_idl      # Our IDL types
    transfer_proto     # Our protobuf transfer types
    glog::glog
    ${MOSQUITTO_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

# ============================================================================
# Verify all required components with CTest
# ============================================================================

enable_testing()

add_test(NAME check_cyclonedds COMMAND ${CMAKE_COMMAND} -E echo "CycloneDDS: OK")
add_test(NAME check_mosquitto COMMAND ${CMAKE_COMMAND} -E echo "libmosquitto: OK")
add_test(NAME check_zstd COMMAND ${CMAKE_COMMAND} -E echo "libzstd: OK")
add_test(NAME check_vssdag COMMAND ${CMAKE_COMMAND} -E echo "libvssdag: OK")
add_test(NAME check_vdr_light COMMAND ${CMAKE_COMMAND} -E echo "vdr-light: OK")

# ============================================================================
# Probes
# ============================================================================

# NOTE: vdr_can_simulator removed - use vdr-light's vdr_vssdag_probe instead
# which provides the same CAN simulation -> libvssdag -> DDS functionality

# vdr_exporter - wrapper that wires subscriber + our compressed sink
add_executable(vdr_exporter
    examples/apps/vdr_exporter/main.cpp
)

target_include_directories(vdr_exporter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vdr_exporter PRIVATE
    vdr_exporter_lib    # Our exporter library
    yaml-cpp::yaml-cpp
    glog::glog
)

# ============================================================================
# Tools (test utilities)
# ============================================================================

# cloud_backend_sim - receives MQTT messages, decodes and displays
add_executable(cloud_backend_sim
    tools/cloud_backend/main.cpp
)

target_include_directories(cloud_backend_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROTO_OUTPUT_DIR}
    ${MOSQUITTO_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
)

target_link_libraries(cloud_backend_sim PRIVATE
    transfer_proto
    nlohmann_json::nlohmann_json
    glog::glog
    ${MOSQUITTO_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

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
        vdr_common            # From vdr-light: DDS wrappers
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
    vdr_common            # From vdr-light: DDS wrappers
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
    COMMAND vdr_exporter --help
)

add_test(NAME integration_cloud_backend_runs
    COMMAND cloud_backend_sim --help
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== COVESA IFEX VDR Integration Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "CycloneDDS: ${CycloneDDS_VERSION}")
message(STATUS "")
message(STATUS "Libraries:")
message(STATUS "  vdr-light: ${VDR_LIGHT_DIR} (vdr_common only)")
message(STATUS "  libvssdag: ${vssdag_FOUND}")
message(STATUS "  libkuksa-cpp: ${kuksa_FOUND}")
message(STATUS "  zstd: ${ZSTD_VERSION}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  Libraries:")
message(STATUS "    - vdr_exporter_lib      (protobuf + zstd compressed MQTT sink)")
if(kuksa_FOUND)
message(STATUS "    - kuksa_dds_bridge_lib  (Kuksa <-> DDS bridge)")
endif()
message(STATUS "    - rt_dds_bridge_lib     (DDS <-> RT transport bridge)")
message(STATUS "  Probes:")
message(STATUS "    - vdr_exporter          (DDS subscriber -> compressed MQTT)")
message(STATUS "    (Use vdr-light's vdr_vssdag_probe for CAN simulation)")
if(kuksa_FOUND)
message(STATUS "    - kuksa_dds_bridge      (Kuksa databroker <-> DDS bridge)")
endif()
message(STATUS "    - rt_dds_bridge         (DDS <-> RT transport bridge)")
message(STATUS "  Tools:")
message(STATUS "    - cloud_backend_sim     (MQTT -> decompress -> display)")
message(STATUS "")
message(STATUS "Kuksa-DDS Bridge: ${KUKSA_BRIDGE_ENABLED}")
message(STATUS "")
message(STATUS "Run 'ctest' after build to verify all components")
message(STATUS "")
