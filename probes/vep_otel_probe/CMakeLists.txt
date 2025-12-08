# VEP OTEL Probe - OpenTelemetry OTLP to DDS
#
# Receives OTLP metrics and logs via gRPC and publishes to DDS.
#
# IDL types come from vep-schema (compiled in top-level CMakeLists.txt)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++ grpc)
endif()

# ============================================================================
# OTLP Proto Library
# ============================================================================

set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_proto)
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# OpenTelemetry proto files location (local to vep-core)
set(OTEL_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")

set(OTLP_PROTO_FILES
    ${OTEL_PROTO_DIR}/opentelemetry/proto/common/v1/common.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/resource/v1/resource.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/metrics/v1/metrics.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/logs/v1/logs.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/collector/metrics/v1/metrics_service.proto
    ${OTEL_PROTO_DIR}/opentelemetry/proto/collector/logs/v1/logs_service.proto
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

add_library(otel_probe_proto STATIC ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(otel_probe_proto PUBLIC ${PROTO_OUTPUT_DIR})

if(gRPC_FOUND)
    target_link_libraries(otel_probe_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
else()
    target_link_libraries(otel_probe_proto PUBLIC ${Protobuf_LIBRARIES} ${GRPC_LIBRARIES})
    target_include_directories(otel_probe_proto PUBLIC ${GRPC_INCLUDE_DIRS})
endif()

# ============================================================================
# Probe executable
# ============================================================================

add_executable(vep_otel_probe main.cpp)

target_include_directories(vep_otel_probe PRIVATE
    ${PROTO_OUTPUT_DIR}
)

target_link_libraries(vep_otel_probe PRIVATE
    vdr_common
    vep_idl
    otel_probe_proto
    glog::glog
)
