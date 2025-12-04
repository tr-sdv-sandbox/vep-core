# VSS CAN Probe - CAN bus to VSS signal transformation
#
# Transforms raw CAN signals into VSS (Vehicle Signal Specification) format
# using libvssdag for DBC parsing and Lua-based transforms.

# ============================================================================
# Compile probe IDL
# ============================================================================

find_program(IDLC_EXECUTABLE idlc REQUIRED)

set(IDL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${IDL_OUTPUT_DIR})

# libvss-types IDL location
set(LIBVSS_TYPES_IDL_DIR "${CMAKE_SOURCE_DIR}/components/libvss-types/idl")

set(VSS_TELEMETRY_IDL ${CMAKE_CURRENT_SOURCE_DIR}/idl/vss_telemetry.idl)
set(VSS_TYPES_IDL ${LIBVSS_TYPES_IDL_DIR}/vss_types.idl)
set(VSS_SIGNAL_IDL ${LIBVSS_TYPES_IDL_DIR}/vss_signal.idl)

# Generated files
set(IDL_SRCS
    ${IDL_OUTPUT_DIR}/vss_types.c
    ${IDL_OUTPUT_DIR}/vss_signal.c
    ${IDL_OUTPUT_DIR}/vss_telemetry.c
)
set(IDL_HDRS
    ${IDL_OUTPUT_DIR}/vss_types.h
    ${IDL_OUTPUT_DIR}/vss_signal.h
    ${IDL_OUTPUT_DIR}/vss_telemetry.h
)

# Compile IDL files
add_custom_command(
    OUTPUT ${IDL_SRCS} ${IDL_HDRS}
    COMMAND ${IDLC_EXECUTABLE} -l c ${VSS_TYPES_IDL}
    COMMAND ${IDLC_EXECUTABLE} -l c -I${LIBVSS_TYPES_IDL_DIR} ${VSS_SIGNAL_IDL}
    COMMAND ${IDLC_EXECUTABLE} -l c -I${LIBVSS_TYPES_IDL_DIR} ${VSS_TELEMETRY_IDL}
    WORKING_DIRECTORY ${IDL_OUTPUT_DIR}
    DEPENDS ${VSS_TYPES_IDL} ${VSS_SIGNAL_IDL} ${VSS_TELEMETRY_IDL}
    COMMENT "Compiling VSS CAN probe IDL"
)

add_library(vss_can_idl STATIC ${IDL_SRCS})
set_target_properties(vss_can_idl PROPERTIES LINKER_LANGUAGE C)
target_include_directories(vss_can_idl PUBLIC ${IDL_OUTPUT_DIR})
target_link_libraries(vss_can_idl PUBLIC CycloneDDS::ddsc)

add_custom_target(generate_vss_can_idl DEPENDS ${IDL_SRCS} ${IDL_HDRS})
add_dependencies(vss_can_idl generate_vss_can_idl)

# ============================================================================
# Probe executable
# ============================================================================

add_executable(vdr_vss_can_probe main.cpp)

target_include_directories(vdr_vss_can_probe PRIVATE
    ${IDL_OUTPUT_DIR}
)

target_link_libraries(vdr_vss_can_probe PRIVATE
    vdr_common
    vss_can_idl
    vssdag
    yaml-cpp::yaml-cpp
    glog::glog
)

# ============================================================================
# Export IDL library for exporter
# ============================================================================

# Make the IDL library available to parent scope
set(VSS_CAN_IDL_TARGET vss_can_idl PARENT_SCOPE)
set(VSS_CAN_IDL_INCLUDE_DIR ${IDL_OUTPUT_DIR} PARENT_SCOPE)
