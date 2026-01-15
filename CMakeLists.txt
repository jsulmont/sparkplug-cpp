# CMakeLists.txt
cmake_minimum_required(VERSION 3.25)

# Platform-specific compiler detection (must be before project())
# By default, use system compiler. Override with -DCMAKE_CXX_COMPILER=...
if(UNIX AND NOT APPLE)
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
    # macOS: Use system libc++ (works with both Apple Clang and MacPorts)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    add_link_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)

    # Support MacPorts (/opt/local) or Homebrew (/opt/homebrew)
    if(EXISTS "/opt/local")
        list(APPEND CMAKE_PREFIX_PATH "/opt/local")
    endif()
    if(EXISTS "/opt/homebrew")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/opt/homebrew/opt/abseil")
    endif()
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

include(cmake/StaticBundle.cmake)

if(NOT BUILD_STATIC_BUNDLE)
    find_package(Protobuf REQUIRED)
    find_package(absl CONFIG QUIET)
    find_package(OpenSSL REQUIRED)
    find_package(eclipse-paho-mqtt-c REQUIRED)
else()
    find_package(OpenSSL REQUIRED)
endif()

# fmt library for std::format compatibility on older platforms
# (macOS < 13.3 lacks to_chars for floating point required by std::format)
if(APPLE)
    # Check if we need fmt (macOS < 13.3)
    # Try to find fmt in MacPorts location
    find_library(FMT_LIBRARY NAMES fmt PATHS /opt/local/lib/libfmt11 /opt/local/lib)
    find_path(FMT_INCLUDE_DIR fmt/format.h PATHS /opt/local/include/libfmt11 /opt/local/include)
    if(FMT_LIBRARY AND FMT_INCLUDE_DIR)
        message(STATUS "Found fmt: ${FMT_LIBRARY}")
        set(SPARKPLUG_USE_FMT ON CACHE BOOL "Use fmt library for format" FORCE)
        add_compile_definitions(SPARKPLUG_USE_FMT)
        # Make fmt available globally for examples and tests
        include_directories(${FMT_INCLUDE_DIR})
        link_libraries(${FMT_LIBRARY})
    else()
        message(STATUS "fmt library not found, using std::format (requires macOS 13.3+)")
    endif()
endif()

add_subdirectory(proto)
target_compile_options(sparkplug_proto PRIVATE -w)
add_subdirectory(src)

if(NOT BUILD_STATIC_BUNDLE)
    add_subdirectory(examples)
    enable_testing()
    add_subdirectory(tests)
endif()

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

