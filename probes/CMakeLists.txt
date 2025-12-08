# Probes - Data acquisition modules
#
# Each probe:
# - Uses IDL types from vep-schema (compiled in top-level CMakeLists.txt)
# - Transforms input data to DDS messages
# - Is self-contained and independently buildable

# ============================================================================
# VEP CAN Probe (CAN bus -> VSS signals)
# ============================================================================

if(TARGET vssdag)
    add_subdirectory(vep_can_probe)
    message(STATUS "  - vep_can_probe (CAN -> VSS -> DDS)")
else()
    message(STATUS "  - vep_can_probe SKIPPED (libvssdag not found)")
endif()

# ============================================================================
# VEP AVTP Probe (IEEE 1722 AVTP -> DDS)
# ============================================================================

add_subdirectory(vep_avtp_probe)
if(TARGET vep_avtp_probe)
    message(STATUS "  - vep_avtp_probe (AVTP -> DDS)")
endif()

# ============================================================================
# VEP OTEL Probe (OpenTelemetry OTLP -> DDS)
# ============================================================================

add_subdirectory(vep_otel_probe)
message(STATUS "  - vep_otel_probe (OTLP -> DDS)")
