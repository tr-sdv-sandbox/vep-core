# Backend Transport Library
# Provides abstract BackendTransport interface and implementations

# Header-only interface library
add_library(vep_backend_transport INTERFACE)
target_include_directories(vep_backend_transport INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# MQTT implementation (requires libmosquitto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

add_library(vep_mqtt_backend_transport STATIC
    src/mqtt_backend_transport.cpp
)

target_include_directories(vep_mqtt_backend_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(vep_mqtt_backend_transport PUBLIC
    vep_backend_transport
    ${MOSQUITTO_LIBRARIES}
    glog::glog
)

target_include_directories(vep_mqtt_backend_transport PRIVATE
    ${MOSQUITTO_INCLUDE_DIRS}
)

# Tests (require MQTT broker)
find_package(GTest QUIET)
if(GTest_FOUND AND VEP_BUILD_TESTS)
    add_executable(test_backend_transport
        tests/transport_test.cpp
    )

    target_link_libraries(test_backend_transport PRIVATE
        vep_mqtt_backend_transport
        GTest::gtest
        GTest::gtest_main
        glog::glog
    )

    # These tests require a running MQTT broker
    add_test(NAME backend_transport_tests COMMAND test_backend_transport)
    set_tests_properties(backend_transport_tests PROPERTIES
        LABELS "integration;mqtt"
    )
endif()
