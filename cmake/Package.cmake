# CPack configuration for creating Debian packages
# This file should be included at the end of the main CMakeLists.txt

set(CPACK_PACKAGE_NAME "libsparkplug-c")
set(CPACK_PACKAGE_VENDOR "Synergy Australia")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Sparkplug B C bindings - Industrial IoT MQTT protocol implementation")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "17")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "sparkplug@synergy.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/synergy-au/sparkplug-cpp")

# Debian-specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Synergy Sparkplug Team <sparkplug@synergy.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Runtime dependencies for Ubuntu 24.04 (Noble)
# These package names are for Ubuntu 24.04 LTS
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libprotobuf33 (>= 3.21.0), libabsl20230802 (>= 20230802), libpaho-mqtt-c1.3 (>= 1.3.0), libssl3 (>= 3.0.0), libc6 (>= 2.38), libstdc++6 (>= 13.0)"
)

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)

# Development package (headers)
set(CPACK_DEBIAN_PACKAGE_NAME "libsparkplug-c-dev")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Description
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"Sparkplug B C bindings library
 This package provides a C interface to the Sparkplug B protocol implementation.
 Sparkplug B is an MQTT-based protocol specification for Industrial IoT applications.
 .
 This library provides:
  - Edge Node API for publishing device data
  - Host Application API for receiving and commanding devices
  - Type-safe payload builders
  - Full TLS/mTLS support
  - Thread-safe operation
")

# RPM-specific settings (for Fedora)
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_PACKAGE_REQUIRES
    "protobuf >= 3.21, abseil-cpp >= 20230802, paho-c >= 1.3, openssl >= 3.0"
)

# Archive package (tar.gz)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

# Component installation
set(CPACK_COMPONENTS_ALL libraries headers)
set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Sparkplug C Libraries")
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C/C++ Header Files")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION "Shared libraries for Sparkplug C bindings")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION "C and C++ header files for development")

# Generator selection
set(CPACK_GENERATOR "TGZ;DEB")

# Source package
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/\\\\.github/"
    "/build.*/"
    "/\\\\.vscode/"
    "/\\\\.DS_Store"
    "/\\\\.gitignore"
)

include(CPack)
