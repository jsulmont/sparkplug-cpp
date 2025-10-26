# proto/CMakeLists.txt
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS sparkplug_b.proto)

add_library(sparkplug_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})

# Enable PIC for linking into shared libraries
set_target_properties(sparkplug_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(sparkplug_proto
    PUBLIC
        protobuf::libprotobuf
)

# Link Abseil if targets exist (newer protobuf versions need it)
if(TARGET absl::log_internal_message)
    target_link_libraries(sparkplug_proto
        PUBLIC
            absl::log_internal_message
            absl::log_internal_check_op
    )
endif()

target_include_directories(sparkplug_proto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)