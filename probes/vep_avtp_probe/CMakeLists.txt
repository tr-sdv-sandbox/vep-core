# VEP AVTP Probe - IEEE 1722 AVTP to DDS
#
# Receives CAN frames tunneled over AVTP (Ethernet) and publishes to DDS.
# Requires Open1722 library.
#
# IDL types come from vep-schema (compiled in top-level CMakeLists.txt)

find_package(Open1722 QUIET)
if(NOT TARGET open1722)
    message(STATUS "Open1722 not found - VEP AVTP probe will not be built")
    return()
endif()

# ============================================================================
# Probe executable
# ============================================================================

add_executable(vep_avtp_probe main.cpp)

target_link_libraries(vep_avtp_probe PRIVATE
    vep_dds_common
    vep_idl
    open1722
    glog::glog
)
