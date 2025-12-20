# Cloud Test Client - simulates cloud side for transport testing

add_executable(cloud_test_client
    main.cpp
)

target_link_libraries(cloud_test_client PRIVATE
    ${MOSQUITTO_LIBRARIES}
    glog::glog
)

target_include_directories(cloud_test_client PRIVATE
    ${MOSQUITTO_INCLUDE_DIRS}
)

# Test
add_test(NAME integration_cloud_test_client_runs
    COMMAND cloud_test_client --help
)
