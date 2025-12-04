# OTEL Probe - OpenTelemetry OTLP to DDS
#
# Receives OTLP metrics and logs via gRPC and publishes to DDS.

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++ grpc)
endif()

# ============================================================================
# Compile probe IDL (uses vss_types.idl from libvss-types)
# ============================================================================

find_program(IDLC_EXECUTABLE idlc REQUIRED)

set(IDL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${IDL_OUTPUT_DIR})

# libvss-types IDL location
set(LIBVSS_TYPES_IDL_DIR "${CMAKE_SOURCE_DIR}/components/libvss-types/idl")

set(OTEL_TELEMETRY_IDL ${CMAKE_CURRENT_SOURCE_DIR}/idl/otel_telemetry.idl)
set(VSS_TYPES_IDL ${LIBVSS_TYPES_IDL_DIR}/vss_types.idl)

# Generated files
set(IDL_SRCS
    ${IDL_OUTPUT_DIR}/vss_types.c
    ${IDL_OUTPUT_DIR}/otel_telemetry.c
)
set(IDL_HDRS
    ${IDL_OUTPUT_DIR}/vss_types.h
    ${IDL_OUTPUT_DIR}/otel_telemetry.h
)

# Compile IDL files in dependency order
add_custom_command(
    OUTPUT ${IDL_SRCS} ${IDL_HDRS}
    COMMAND ${IDLC_EXECUTABLE} -l c ${VSS_TYPES_IDL}
    COMMAND ${IDLC_EXECUTABLE} -l c -I${LIBVSS_TYPES_IDL_DIR} ${OTEL_TELEMETRY_IDL}
    WORKING_DIRECTORY ${IDL_OUTPUT_DIR}
    DEPENDS ${VSS_TYPES_IDL} ${OTEL_TELEMETRY_IDL}
    COMMENT "Compiling OTEL probe IDL (with libvss-types)"
)

add_library(otel_idl STATIC ${IDL_SRCS})
set_target_properties(otel_idl PROPERTIES LINKER_LANGUAGE C)
target_include_directories(otel_idl PUBLIC ${IDL_OUTPUT_DIR})
target_link_libraries(otel_idl PUBLIC CycloneDDS::ddsc)

add_custom_target(generate_otel_idl DEPENDS ${IDL_SRCS} ${IDL_HDRS})
add_dependencies(otel_idl generate_otel_idl)

# ============================================================================
# OTLP Proto Library
# ============================================================================

set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_proto)
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# OpenTelemetry proto files location
set(OTEL_PROTO_DIR "${CMAKE_SOURCE_DIR}/components/vep-dds/proto")

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

add_executable(vdr_otel_probe main.cpp)

target_include_directories(vdr_otel_probe PRIVATE
    ${IDL_OUTPUT_DIR}
    ${PROTO_OUTPUT_DIR}
)

target_link_libraries(vdr_otel_probe PRIVATE
    vdr_common
    otel_idl
    otel_probe_proto
    glog::glog
)

# ============================================================================
# Export IDL library for exporter
# ============================================================================

set(OTEL_IDL_TARGET otel_idl PARENT_SCOPE)
set(OTEL_IDL_INCLUDE_DIR ${IDL_OUTPUT_DIR} PARENT_SCOPE)
