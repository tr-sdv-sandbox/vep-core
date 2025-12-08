# VEP CAN Probe - CAN bus to VSS signal transformation
#
# Transforms raw CAN signals into VSS (Vehicle Signal Specification) format
# using libvssdag for DBC parsing and Lua-based transforms.
#
# IDL types come from vep-schema (compiled in top-level CMakeLists.txt)

# ============================================================================
# Probe executable
# ============================================================================

add_executable(vep_can_probe main.cpp)

target_link_libraries(vep_can_probe PRIVATE
    vep_dds_common
    vep_idl
    vssdag
    yaml-cpp::yaml-cpp
    glog::glog
)
