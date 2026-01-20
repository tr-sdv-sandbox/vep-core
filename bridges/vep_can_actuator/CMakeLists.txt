# Copyright 2025 Vehicle Edge Platform Contributors
# SPDX-License-Identifier: Apache-2.0
#
# vep_can_actuator - DDS to CAN actuator bridge
#
# Subscribes to DDS actuator topic and writes encoded values to CAN interface.

# Library with parser, encoder, and writer
add_library(vep_can_actuator_lib STATIC
    vss_dbc_parser.cpp
    can_encoder.cpp
    can_writer.cpp
)

target_include_directories(vep_can_actuator_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vep_can_actuator_lib PUBLIC
    vep_dds_common              # DDS wrappers
    vep_idl                     # IDL types (vep_VssSignal, etc.)
    nlohmann_json::nlohmann_json
    glog::glog
)

# Main executable
add_executable(vep_can_actuator main.cpp)

target_link_libraries(vep_can_actuator PRIVATE
    vep_can_actuator_lib
    gflags
    glog::glog
)

# Install
install(TARGETS vep_can_actuator
    RUNTIME DESTINATION bin
)

# Test publisher tool
add_executable(actuator_test_publisher
    tools/actuator_test_publisher.cpp
)

target_link_libraries(actuator_test_publisher PRIVATE
    vep_dds_common
    vep_idl
    gflags
    glog::glog
)

install(TARGETS actuator_test_publisher
    RUNTIME DESTINATION bin
)

# KUKSA actuator sender tool (uses KUKSA v2 Actuate API)
if(TARGET kuksa)
    add_executable(kuksa_actuator_sender
        tools/kuksa_actuator_sender.cpp
    )

    target_link_libraries(kuksa_actuator_sender PRIVATE
        kuksa
        gflags
        glog::glog
    )

    install(TARGETS kuksa_actuator_sender
        RUNTIME DESTINATION bin
    )
endif()

# Test that executable runs (--help exits with 1, so we just check it doesn't crash)
add_test(NAME integration_vep_can_actuator_runs
    COMMAND vep_can_actuator --help
)
set_tests_properties(integration_vep_can_actuator_runs PROPERTIES
    WILL_FAIL TRUE
)

# Unit tests (if GTest available and tests enabled)
if(GTest_FOUND AND VEP_BUILD_TESTS)
    # Test JSON parser
    add_executable(test_vss_dbc_parser
        tests/test_vss_dbc_parser.cpp
    )
    target_link_libraries(test_vss_dbc_parser PRIVATE
        vep_can_actuator_lib
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME unit_vss_dbc_parser COMMAND test_vss_dbc_parser)

    # Test CAN encoder
    add_executable(test_can_encoder
        tests/test_can_encoder.cpp
    )
    target_link_libraries(test_can_encoder PRIVATE
        vep_can_actuator_lib
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME unit_can_encoder COMMAND test_can_encoder)
endif()
