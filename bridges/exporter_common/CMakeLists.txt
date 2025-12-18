# exporter_common - Reusable exporter libraries
#
# Provides:
#   - vep_exporter_common: Core encoding/batching/compression (no transport)
#   - vep_mqtt_sink: MQTT transport implementation
#
# Usage:
#   target_link_libraries(my_exporter PRIVATE vep_exporter_common vep_mqtt_sink)

# ============================================================================
# Core library (encoding, batching, compression - transport agnostic)
# ============================================================================

add_library(vep_exporter_common STATIC
    src/wire_encoder.cpp
    src/wire_decoder.cpp
    src/batch_builder.cpp
    src/compressor.cpp
    src/exporter_pipeline.cpp
    src/subscriber.cpp
)

target_include_directories(vep_exporter_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROTO_OUTPUT_DIR}
    ${ZSTD_INCLUDE_DIRS}
)

target_link_libraries(vep_exporter_common PUBLIC
    vep_dds_common     # DDS wrappers (for SubscriptionManager)
    vep_idl            # DDS IDL types
    transfer_proto     # Protobuf wire format
    glog::glog
    ${ZSTD_LIBRARIES}
)

# ============================================================================
# MQTT transport sink
# ============================================================================

add_library(vep_mqtt_sink STATIC
    src/mqtt_sink.cpp
)

target_include_directories(vep_mqtt_sink PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MOSQUITTO_INCLUDE_DIRS}
)

target_link_libraries(vep_mqtt_sink PUBLIC
    vep_exporter_common
    glog::glog
    ${MOSQUITTO_LIBRARIES}
)

# ============================================================================
# Unit Tests
# ============================================================================

find_package(GTest QUIET)
if(GTest_FOUND AND VEP_BUILD_TESTS)
    # Compressor tests
    add_executable(test_compressor
        tests/compressor_test.cpp
    )
    target_link_libraries(test_compressor PRIVATE
        vep_exporter_common
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME exporter_common_compressor_tests COMMAND test_compressor)

    # Batch builder tests
    add_executable(test_batch_builder
        tests/batch_builder_test.cpp
    )
    target_link_libraries(test_batch_builder PRIVATE
        vep_exporter_common
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME exporter_common_batch_builder_tests COMMAND test_batch_builder)

    # Wire encoder/decoder round-trip tests
    add_executable(test_wire_codec
        tests/wire_codec_test.cpp
    )
    target_link_libraries(test_wire_codec PRIVATE
        vep_exporter_common
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME exporter_common_wire_codec_tests COMMAND test_wire_codec)

    message(STATUS "  - exporter_common unit tests (compressor, batch_builder, wire_codec)")
endif()
