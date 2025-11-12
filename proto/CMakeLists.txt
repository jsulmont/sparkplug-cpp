# proto/CMakeLists.txt
if(BUILD_STATIC_BUNDLE)
    find_program(PROTOC_BIN protoc REQUIRED)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sparkplug_b.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/sparkplug_b.pb.h
        COMMAND ${PROTOC_BIN}
        ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR} -I${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sparkplug_b.proto
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sparkplug_b.proto
        COMMENT "Generating protobuf code with system protoc"
    )
    add_library(sparkplug_proto STATIC
        ${CMAKE_CURRENT_BINARY_DIR}/sparkplug_b.pb.cc
        ${CMAKE_CURRENT_BINARY_DIR}/sparkplug_b.pb.h
    )
    target_include_directories(sparkplug_proto PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${protobuf_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    )
else()
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS sparkplug_b.proto)
    add_library(sparkplug_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
endif()

# Enable PIC for linking into shared libraries
set_target_properties(sparkplug_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_STATIC_BUNDLE)
    target_link_libraries(sparkplug_proto PUBLIC libprotobuf)
else()
    target_link_libraries(sparkplug_proto PUBLIC protobuf::libprotobuf)
    if(TARGET absl::log_internal_message)
        target_link_libraries(sparkplug_proto
            PUBLIC
                absl::log_internal_message
                absl::log_internal_check_op
        )
    endif()
endif()

if(NOT BUILD_STATIC_BUNDLE)
    target_include_directories(sparkplug_proto
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:include>
    )
endif()
