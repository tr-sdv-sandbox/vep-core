# Backend Transport Library
# Provides abstract BackendTransport interface
# Note: MqttBackendTransport deprecated - use IFEX backend-transport-service instead

# Header-only interface library
add_library(vep_backend_transport INTERFACE)
target_include_directories(vep_backend_transport INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
