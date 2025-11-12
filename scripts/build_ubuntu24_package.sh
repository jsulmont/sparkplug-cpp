#!/bin/bash
# Build script for creating Ubuntu 24.04 (Noble) packages
# This script can be run in a Docker container for cross-platform builds

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-ubuntu24"

echo "Building Sparkplug C bindings for Ubuntu 24.04"

# Check if running in Ubuntu 24.04
if [ -f /etc/os-release ]; then
    . /etc/os-release
    if [ "$ID" = "ubuntu" ] && [ "$VERSION_ID" = "24.04" ]; then
        echo "Detected Ubuntu 24.04 - proceeding with native build"
    else
        echo "Warning: Not running on Ubuntu 24.04 (detected: $ID $VERSION_ID)"
        echo "Package dependencies may need adjustment"
    fi
fi

# Install build dependencies (if running on Ubuntu)
if [ "$EUID" -eq 0 ] || command -v sudo &> /dev/null; then
    echo "Installing build dependencies..."
    ${SUDO:-sudo} apt-get update
    ${SUDO:-sudo} apt-get install -y \
        build-essential \
        cmake \
        git \
        libprotobuf-dev \
        protobuf-compiler \
        libabsl-dev \
        libpaho-mqtt-dev \
        libssl-dev \
        clang-18 \
        libc++-18-dev \
        libc++abi-18-dev
else
    echo "Skipping dependency installation (no sudo access)"
    echo "Ensure the following packages are installed:"
    echo "  build-essential cmake git libprotobuf-dev protobuf-compiler"
    echo "  libabsl-dev libpaho-mqtt-dev libssl-dev clang-18 libc++-18-dev"
fi

# Clean previous build
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Configure with clang-18 (better C++23 support than GCC 13 on Ubuntu 24.04)
echo "Configuring CMake..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_SHARED_LIBS=ON \
    -DCPACK_GENERATOR="DEB;TGZ"

# Build
echo "Building..."
cmake --build "${BUILD_DIR}" --target sparkplug_c -j$(nproc)

# Run tests (optional)
if [ "${SKIP_TESTS:-0}" = "0" ]; then
    echo "Running tests..."
    # Start mosquitto if available
    if command -v mosquitto &> /dev/null; then
        mosquitto -c /dev/null -p 1883 &
        MOSQUITTO_PID=$!
        sleep 2
        ctest --test-dir "${BUILD_DIR}" --output-on-failure || true
        kill $MOSQUITTO_PID 2>/dev/null || true
    else
        echo "Mosquitto not available, skipping tests"
    fi
fi

# Create packages
echo "Creating packages..."
cd "${BUILD_DIR}"
cpack -G "DEB;TGZ"

echo ""
echo "SUCCESS! Packages created in ${BUILD_DIR}:"
ls -lh "${BUILD_DIR}"/*.deb "${BUILD_DIR}"/*.tar.gz 2>/dev/null || true

echo ""
echo "To install the .deb package:"
echo "  sudo dpkg -i ${BUILD_DIR}/libsparkplug-c-*.deb"
echo "  sudo apt-get install -f  # Install any missing dependencies"
echo ""
echo "Package info:"
dpkg-deb --info "${BUILD_DIR}"/libsparkplug-c-*.deb 2>/dev/null | head -20 || true
