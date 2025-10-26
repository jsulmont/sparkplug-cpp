# tests/CMakeLists.txt

# Topic parsing tests
add_executable(test_topic test_topic.cpp)
target_link_libraries(test_topic PRIVATE sparkplug_cpp)
add_test(NAME TopicTest COMMAND test_topic)

# Sparkplug 2.2 compliance tests
add_executable(test_compliance test_compliance.cpp)
target_link_libraries(test_compliance PRIVATE sparkplug_cpp)
add_test(NAME ComplianceTest COMMAND test_compliance)

# PayloadBuilder unit tests
add_executable(test_payload_builder test_payload_builder.cpp)
target_link_libraries(test_payload_builder PRIVATE sparkplug_cpp)
add_test(NAME PayloadBuilderTest COMMAND test_payload_builder)

# Error handling tests
add_executable(test_error_handling test_error_handling.cpp)
target_link_libraries(test_error_handling PRIVATE sparkplug_cpp)
add_test(NAME ErrorHandlingTest COMMAND test_error_handling)

# Device-level API tests (DBIRTH/DDATA/DDEATH)
add_executable(test_device_apis test_device_apis.cpp)
target_link_libraries(test_device_apis PRIVATE sparkplug_cpp)
add_test(NAME DeviceApisTest COMMAND test_device_apis)

# Command handling tests (NCMD/DCMD)
add_executable(test_command_handling test_command_handling.cpp)
target_link_libraries(test_command_handling PRIVATE sparkplug_cpp)
add_test(NAME CommandHandlingTest COMMAND test_command_handling)

# C API tests
add_executable(test_c_api test_c_api.c)
target_link_libraries(test_c_api PRIVATE sparkplug_c)
add_test(NAME CApiTest COMMAND test_c_api)