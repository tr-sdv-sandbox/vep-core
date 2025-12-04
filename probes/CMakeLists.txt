# Probes - Data acquisition modules
#
# Each probe:
# - Defines its own IDL for DDS message types
# - Transforms input data to DDS messages
# - Is self-contained and independently buildable

# ============================================================================
# VSS CAN Probe (CAN bus -> VSS signals)
# ============================================================================

if(TARGET vssdag)
    add_subdirectory(vss_can)
    message(STATUS "  - vdr_vss_can_probe (CAN -> VSS -> DDS)")
else()
    message(STATUS "  - vdr_vss_can_probe SKIPPED (libvssdag not found)")
endif()

# ============================================================================
# AVTP Probe (IEEE 1722 AVTP -> DDS)
# ============================================================================

add_subdirectory(avtp)
if(TARGET vdr_avtp_probe)
    message(STATUS "  - vdr_avtp_probe (AVTP -> DDS)")
endif()

# ============================================================================
# OTEL Probe (OpenTelemetry OTLP -> DDS)
# ============================================================================

add_subdirectory(otel)
message(STATUS "  - vdr_otel_probe (OTLP -> DDS)")

# ============================================================================
# Collect all probe IDL targets for exporter
# ============================================================================

# These variables are set by each probe's CMakeLists.txt
# and used by the exporter to link all IDL types
set(PROBE_IDL_TARGETS "" PARENT_SCOPE)
set(PROBE_IDL_INCLUDE_DIRS "" PARENT_SCOPE)

if(TARGET vss_can_idl)
    list(APPEND PROBE_IDL_TARGETS vss_can_idl)
    list(APPEND PROBE_IDL_INCLUDE_DIRS ${VSS_CAN_IDL_INCLUDE_DIR})
endif()

if(TARGET avtp_idl)
    list(APPEND PROBE_IDL_TARGETS avtp_idl)
    list(APPEND PROBE_IDL_INCLUDE_DIRS ${AVTP_IDL_INCLUDE_DIR})
endif()

if(TARGET otel_idl)
    list(APPEND PROBE_IDL_TARGETS otel_idl)
    list(APPEND PROBE_IDL_INCLUDE_DIRS ${OTEL_IDL_INCLUDE_DIR})
endif()

set(PROBE_IDL_TARGETS ${PROBE_IDL_TARGETS} PARENT_SCOPE)
set(PROBE_IDL_INCLUDE_DIRS ${PROBE_IDL_INCLUDE_DIRS} PARENT_SCOPE)
