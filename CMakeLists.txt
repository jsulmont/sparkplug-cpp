# CMakeLists.txt
cmake_minimum_required(VERSION 3.25)

# Platform-specific compiler detection (must be before project())
if(APPLE)
    # macOS: Use Homebrew LLVM if not specified
    if(NOT DEFINED CMAKE_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm/bin/clang++)
    endif()
    if(NOT DEFINED CMAKE_C_COMPILER)
        set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm/bin/clang)
    endif()
elseif(UNIX)
    # Linux: Prefer clang-18 if not specified
    if(NOT DEFINED CMAKE_CXX_COMPILER)
        find_program(CLANG_CXX NAMES clang++-18 clang++)
        if(CLANG_CXX)
            set(CMAKE_CXX_COMPILER ${CLANG_CXX})
        endif()
    endif()
    if(NOT DEFINED CMAKE_C_COMPILER)
        find_program(CLANG_C NAMES clang-18 clang)
        if(CLANG_C)
            set(CMAKE_C_COMPILER ${CLANG_C})
        endif()
    endif()
endif()

project(SparkplugB VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform-specific flags
if(APPLE)
    # macOS: Use Homebrew LLVM libc++
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    add_link_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    add_link_options($<$<COMPILE_LANGUAGE:CXX>:-L/opt/homebrew/opt/llvm/lib/c++>)
    add_link_options($<$<COMPILE_LANGUAGE:CXX>:-Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++>)

    set(CMAKE_PREFIX_PATH "/opt/homebrew;/opt/homebrew/opt/abseil")
    set(Protobuf_INCLUDE_DIR "/opt/homebrew/include")
    set(Protobuf_PROTOC_EXECUTABLE "/opt/homebrew/bin/protoc")
elseif(UNIX)
    # Linux: No special flags needed (use distribution's default stdlib)
endif()

# Common warning flags
add_compile_options(-Wall -Wextra -Wpedantic)

# C++ specific warnings
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-c++98-compat>)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch tl::expected for C++23 std::expected backport on older compilers
include(FetchContent)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "Build tl::expected tests" FORCE)
FetchContent_Declare(
    tl-expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG v1.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tl-expected)

find_package(Protobuf REQUIRED)
find_package(absl CONFIG QUIET)  # Optional - newer protobuf needs it
find_package(OpenSSL REQUIRED)   # Required for TLS/SSL support
find_package(eclipse-paho-mqtt-c REQUIRED)

add_subdirectory(proto)
target_compile_options(sparkplug_proto PRIVATE -w)
add_subdirectory(src)
add_subdirectory(examples)

enable_testing()
add_subdirectory(tests)

# Doxygen documentation target
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    message(STATUS "Doxygen found - added 'docs' target. Run 'cmake --build build --target docs'")
else()
    message(STATUS "Doxygen not found - 'docs' target not available")
endif()

