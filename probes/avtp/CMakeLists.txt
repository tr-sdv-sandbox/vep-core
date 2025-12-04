# AVTP Probe - IEEE 1722 AVTP to DDS
#
# Receives CAN frames tunneled over AVTP (Ethernet) and publishes to DDS.
# Requires Open1722 library.

find_package(Open1722 QUIET)
if(NOT TARGET open1722)
    message(STATUS "Open1722 not found - AVTP probe will not be built")
    return()
endif()

# ============================================================================
# Compile probe IDL (uses vss_types.idl from libvss-types)
# ============================================================================

find_program(IDLC_EXECUTABLE idlc REQUIRED)

set(IDL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${IDL_OUTPUT_DIR})

# libvss-types IDL location
set(LIBVSS_TYPES_IDL_DIR "${CMAKE_SOURCE_DIR}/components/libvss-types/idl")

set(AVTP_TELEMETRY_IDL ${CMAKE_CURRENT_SOURCE_DIR}/idl/avtp_telemetry.idl)
set(VSS_TYPES_IDL ${LIBVSS_TYPES_IDL_DIR}/vss_types.idl)

# Generated files
set(IDL_SRCS
    ${IDL_OUTPUT_DIR}/vss_types.c
    ${IDL_OUTPUT_DIR}/avtp_telemetry.c
)
set(IDL_HDRS
    ${IDL_OUTPUT_DIR}/vss_types.h
    ${IDL_OUTPUT_DIR}/avtp_telemetry.h
)

# Compile IDL files in dependency order
add_custom_command(
    OUTPUT ${IDL_SRCS} ${IDL_HDRS}
    COMMAND ${IDLC_EXECUTABLE} -l c ${VSS_TYPES_IDL}
    COMMAND ${IDLC_EXECUTABLE} -l c -I${LIBVSS_TYPES_IDL_DIR} ${AVTP_TELEMETRY_IDL}
    WORKING_DIRECTORY ${IDL_OUTPUT_DIR}
    DEPENDS ${VSS_TYPES_IDL} ${AVTP_TELEMETRY_IDL}
    COMMENT "Compiling AVTP probe IDL (with libvss-types)"
)

add_library(avtp_idl STATIC ${IDL_SRCS})
set_target_properties(avtp_idl PROPERTIES LINKER_LANGUAGE C)
target_include_directories(avtp_idl PUBLIC ${IDL_OUTPUT_DIR})
target_link_libraries(avtp_idl PUBLIC CycloneDDS::ddsc)

add_custom_target(generate_avtp_idl DEPENDS ${IDL_SRCS} ${IDL_HDRS})
add_dependencies(avtp_idl generate_avtp_idl)

# ============================================================================
# Probe executable
# ============================================================================

add_executable(vdr_avtp_probe main.cpp)

target_include_directories(vdr_avtp_probe PRIVATE
    ${IDL_OUTPUT_DIR}
)

target_link_libraries(vdr_avtp_probe PRIVATE
    vdr_common
    avtp_idl
    open1722
    glog::glog
)

# ============================================================================
# Export IDL library for exporter
# ============================================================================

set(AVTP_IDL_TARGET avtp_idl PARENT_SCOPE)
set(AVTP_IDL_INCLUDE_DIR ${IDL_OUTPUT_DIR} PARENT_SCOPE)
