#!/bin/bash
# Script to build a release package of the sparkplug-cpp C bindings
# Creates a tarball suitable for distribution and use by sparkplug-rs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION="${VERSION:-$(git -C "${PROJECT_ROOT}" describe --tags --always)}"
PLATFORM="${PLATFORM:-$(uname -s | tr '[:upper:]' '[:lower:]')}"
ARCH="${ARCH:-$(uname -m)}"

echo "Building sparkplug-cpp release package"
echo "  Version: ${VERSION}"
echo "  Platform: ${PLATFORM}"
echo "  Arch: ${ARCH}"

BUILD_DIR="${PROJECT_ROOT}/build-release-package"
INSTALL_PREFIX="${BUILD_DIR}/install"
PACKAGE_NAME="sparkplug-cpp-c-bindings-${VERSION}-${PLATFORM}-${ARCH}"
PACKAGE_DIR="${BUILD_DIR}/${PACKAGE_NAME}"

# Clean previous build
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Configure CMake with install prefix
echo "Configuring CMake..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DBUILD_SHARED_LIBS=ON

# Build only the C library target
echo "Building sparkplug_c library..."
cmake --build "${BUILD_DIR}" --target sparkplug_c --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Install to prefix
echo "Installing to ${INSTALL_PREFIX}..."
cmake --install "${BUILD_DIR}" --config Release

# Create package directory structure
echo "Creating package structure..."
mkdir -p "${PACKAGE_DIR}"/{lib,include}

# Copy installed files
cp -r "${INSTALL_PREFIX}/lib/"* "${PACKAGE_DIR}/lib/"
cp -r "${INSTALL_PREFIX}/include/"* "${PACKAGE_DIR}/include/"

# Create README for the package
cat > "${PACKAGE_DIR}/README.md" << 'EOF'
# Sparkplug B C Bindings

Prebuilt binary package of the Sparkplug B C bindings from sparkplug-cpp.

## Contents

- `lib/` - Shared library files (libsparkplug_c.so/dylib)
- `include/` - C header files (sparkplug_c.h and dependencies)

## Dependencies

Runtime dependencies required to use this library:

### Ubuntu 24.04
```bash
sudo apt-get install \
    libprotobuf33 \
    libabsl20230802 \
    libpaho-mqtt-c1.3 \
    libssl3
```

### Fedora 40+
```bash
sudo dnf install \
    protobuf \
    abseil-cpp \
    paho-c \
    openssl
```

### macOS (Homebrew)
```bash
brew install \
    protobuf \
    abseil \
    paho-mqtt-c \
    openssl@3
```

## Usage

### Manual Installation

```bash
# Extract package
tar xzf sparkplug-cpp-c-bindings-*.tar.gz
cd sparkplug-cpp-c-bindings-*

# Copy to system directories (requires sudo)
sudo cp -r lib/* /usr/local/lib/
sudo cp -r include/* /usr/local/include/
sudo ldconfig  # Linux only
```

### Use with Rust (sparkplug-rs)

Set environment variables to point to this package:

```bash
export SPARKPLUG_CPP_LIB_DIR=/path/to/package/lib
export SPARKPLUG_CPP_INCLUDE_DIR=/path/to/package/include
```

Then in your Rust project's build.rs:

```rust
let lib_dir = env::var("SPARKPLUG_CPP_LIB_DIR")
    .unwrap_or_else(|_| "/usr/local/lib".to_string());
let include_dir = env::var("SPARKPLUG_CPP_INCLUDE_DIR")
    .unwrap_or_else(|_| "/usr/local/include".to_string());

println!("cargo:rustc-link-search=native={}", lib_dir);
println!("cargo:rustc-link-lib=dylib=sparkplug_c");
```

## Building from Source

To build this package from source, see:
https://github.com/synergy-au/sparkplug-cpp

## License

Apache-2.0
EOF

# Create build info file
cat > "${PACKAGE_DIR}/BUILD_INFO.txt" << EOF
Build Information
=================
Version: ${VERSION}
Platform: ${PLATFORM}
Architecture: ${ARCH}
Build Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
Build Host: $(hostname)
Compiler: $(${CXX:-c++} --version | head -1)
EOF

# Create tarball
echo "Creating tarball..."
cd "${BUILD_DIR}"
tar czf "${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"

echo ""
echo "SUCCESS! Package created:"
echo "  ${BUILD_DIR}/${PACKAGE_NAME}.tar.gz"
echo ""
echo "Package contents:"
tar tzf "${PACKAGE_NAME}.tar.gz" | head -20
echo ""
echo "To test:"
echo "  cd ${BUILD_DIR}"
echo "  tar xzf ${PACKAGE_NAME}.tar.gz"
echo "  ls -lR ${PACKAGE_NAME}/"
