# vep_exporter_ifex - DDS to IFEX BackendTransport exporter
# Uses vep-core's UnifiedExporterPipeline with IfexBackendTransport (gRPC client)

# =============================================================================
# Find required packages
# =============================================================================
find_package(gRPC REQUIRED)
find_package(glog REQUIRED)

# =============================================================================
# IfexBackendTransport library (wrapper around IFEX BackendTransportClient)
# =============================================================================
add_library(ifex_backend_transport STATIC
    src/ifex_backend_transport.cpp
)

target_include_directories(ifex_backend_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ifex_backend_transport PUBLIC
    vep_backend_transport          # BackendTransport interface from vep-core
    ifex-backend-transport-client  # IFEX gRPC client from covesa-ifex-core
    gRPC::grpc++
    glog::glog
)

target_compile_options(ifex_backend_transport PRIVATE -Wall -Wextra -Wpedantic)

# =============================================================================
# Main exporter executable
# =============================================================================
add_executable(vep_exporter_ifex
    src/main.cpp
)

target_link_libraries(vep_exporter_ifex PRIVATE
    ifex_backend_transport
    vep_exporter_common
    vep_dds_common
    glog::glog
)

# =============================================================================
# Install
# =============================================================================
install(TARGETS vep_exporter_ifex RUNTIME DESTINATION bin)
