# VEP Host Metrics - Linux host metrics collector
#
# Collects CPU, memory, disk, and network metrics from Linux /proc filesystem
# and exports them via OTLP gRPC to vep_otel_probe.
#
# Based on OpenTelemetry host-metrics semantic conventions.

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++ grpc)
endif()

# ============================================================================
# OTLP Proto Library (client-side)
# ============================================================================

set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_proto)
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# OpenTelemetry proto files location (shared with vep_otel_probe)
set(OTEL_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")

set(OTLP_PROTO_FILES
    ${OTEL_PROTO_DIR}/opentelemetry/proto/common/v1/common.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/resource/v1/resource.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/metrics/v1/metrics.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/collector/metrics/v1/metrics_service.proto
)

set(PROTO_SRCS)
set(GRPC_SRCS)

foreach(PROTO_FILE ${OTLP_PROTO_FILES})
    get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    file(RELATIVE_PATH PROTO_REL_DIR ${OTEL_PROTO_DIR} ${PROTO_DIR})

    set(PROTO_SRC ${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.cc)
    list(APPEND PROTO_SRCS ${PROTO_SRC})

    if(PROTO_FILE MATCHES "service\\.proto$")
        set(GRPC_SRC ${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.grpc.pb.cc)
        list(APPEND GRPC_SRCS ${GRPC_SRC})

        add_custom_command(
            OUTPUT ${PROTO_SRC} ${GRPC_SRC}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}
            COMMAND ${PROTOC_EXECUTABLE}
                --proto_path=${OTEL_PROTO_DIR}
                --cpp_out=${PROTO_OUTPUT_DIR}
                --grpc_out=${PROTO_OUTPUT_DIR}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                ${PROTO_FILE}
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating protobuf and gRPC: ${PROTO_FILE}"
        )
    else()
        add_custom_command(
            OUTPUT ${PROTO_SRC}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}
            COMMAND ${PROTOC_EXECUTABLE}
                --proto_path=${OTEL_PROTO_DIR}
                --cpp_out=${PROTO_OUTPUT_DIR}
                ${PROTO_FILE}
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating protobuf: ${PROTO_FILE}"
        )
    endif()
endforeach()

add_library(host_metrics_proto STATIC ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(host_metrics_proto PUBLIC ${PROTO_OUTPUT_DIR})

if(gRPC_FOUND)
    target_link_libraries(host_metrics_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
else()
    target_link_libraries(host_metrics_proto PUBLIC ${Protobuf_LIBRARIES} ${GRPC_LIBRARIES})
    target_include_directories(host_metrics_proto PUBLIC ${GRPC_INCLUDE_DIRS})
endif()

# ============================================================================
# Host Metrics Library
# ============================================================================

add_library(vep_host_metrics_lib STATIC
    src/proc_reader.cpp
    src/otlp_metrics_client.cpp
)

target_include_directories(vep_host_metrics_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROTO_OUTPUT_DIR}
)

target_link_libraries(vep_host_metrics_lib PUBLIC
    host_metrics_proto
    glog::glog
)

# ============================================================================
# Host Metrics Executable
# ============================================================================

add_executable(vep_host_metrics main.cpp)

target_link_libraries(vep_host_metrics PRIVATE
    vep_host_metrics_lib
    glog::glog
)

# ============================================================================
# Tests
# ============================================================================

add_test(NAME integration_vep_host_metrics_runs
    COMMAND vep_host_metrics --help
)
