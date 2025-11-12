#!/bin/bash
# Build fully static C library bundle for musl/Alpine Linux
# Creates a single .a file with all dependencies bundled

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION="${VERSION:-$(git -C "${PROJECT_ROOT}" describe --tags --always)}"
PLATFORM="linux-musl"
ARCH="${ARCH:-$(uname -m)}"

echo "Building sparkplug-cpp static musl bundle"
echo "  Version: ${VERSION}"
echo "  Platform: ${PLATFORM}"
echo "  Arch: ${ARCH}"

BUILD_DIR="${PROJECT_ROOT}/build-static-musl"
PACKAGE_NAME="sparkplug-c-${VERSION}-${PLATFORM}-${ARCH}-static"
PACKAGE_DIR="${BUILD_DIR}/${PACKAGE_NAME}"

# Clean previous build
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Configure with static bundle
echo "Configuring CMake for static bundle..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_STATIC_BUNDLE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-static" \
    -DCMAKE_FIND_LIBRARY_SUFFIXES=".a"

# Build
echo "Building static libraries..."
cmake --build "${BUILD_DIR}" -j$(nproc 2>/dev/null || echo 4)

# Create combined static archive
echo "Creating combined static archive..."
BUNDLE_DIR="${BUILD_DIR}/bundle_objects"
mkdir -p "${BUNDLE_DIR}"
cd "${BUNDLE_DIR}"

# Extract all object files from dependencies
echo "  Extracting sparkplug_c_bundle..."
ar x "${BUILD_DIR}/src/libsparkplug_c_bundle.a"

echo "  Extracting paho-mqtt-c..."
ar x "${BUILD_DIR}/_deps/paho-mqtt-c-build/src/libpaho-mqtt3as-static.a" 2>/dev/null || \
    ar x "${BUILD_DIR}/_deps/paho-mqtt-c-build/src/libpaho-mqtt3as.a"

echo "  Extracting protobuf..."
ar x "${BUILD_DIR}/_deps/protobuf-build/libprotobuf.a"

echo "  Extracting abseil libraries..."
find "${BUILD_DIR}/_deps/abseil-cpp-build" -name "libabsl_*.a" -exec sh -c 'ar x "$1"' _ {} \;

# Create combined archive
echo "  Creating final archive..."
ar rcs libsparkplug_c_static_bundle.a *.o *.obj 2>/dev/null || ar rcs libsparkplug_c_static_bundle.a *.o
ranlib libsparkplug_c_static_bundle.a

# Create package directory
echo "Creating package structure..."
mkdir -p "${PACKAGE_DIR}"/{lib,include}

# Copy library and headers
cp libsparkplug_c_static_bundle.a "${PACKAGE_DIR}/lib/"
cp "${PROJECT_ROOT}/include/sparkplug/sparkplug_c.h" "${PACKAGE_DIR}/include/"

# Create README
cat > "${PACKAGE_DIR}/README.md" << 'EOF'
# Sparkplug B C Static Library (musl)

Fully static library bundle with all dependencies included.

## Features

- Single `.a` file with Sparkplug C API, Paho MQTT, Protobuf, and Abseil
- No runtime dependencies except libc (musl or glibc)
- Ideal for containers, embedded systems, and static linking
- Compatible with Alpine Linux and other musl-based distributions

## File Contents

- `lib/libsparkplug_c_static_bundle.a` - Complete static library
- `include/sparkplug_c.h` - C API header

## Usage

### With GCC/Clang

```bash
gcc myapp.c -L./lib -lsparkplug_c_static_bundle -lssl -lcrypto -lpthread -ldl -o myapp
```

### With Rust (sparkplug-rs build.rs)

```rust
use std::env;

fn main() {
    let lib_dir = env::var("SPARKPLUG_LIB_DIR")
        .unwrap_or_else(|_| "/usr/local/lib".to_string());

    println!("cargo:rustc-link-search=native={}", lib_dir);
    println!("cargo:rustc-link-lib=static=sparkplug_c_static_bundle");

    // OpenSSL is the only external dependency
    println!("cargo:rustc-link-lib=ssl");
    println!("cargo:rustc-link-lib=crypto");
}
```

Then set the environment variable:
```bash
export SPARKPLUG_LIB_DIR=/path/to/this/package/lib
cargo build
```

### With CMake

```cmake
add_executable(myapp main.c)
target_link_libraries(myapp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libsparkplug_c_static_bundle.a
    ssl
    crypto
    pthread
    dl
)
```

## Installation

```bash
# System-wide (requires root)
sudo cp lib/libsparkplug_c_static_bundle.a /usr/local/lib/
sudo cp include/sparkplug_c.h /usr/local/include/

# Or use in-place with -L and -I flags
gcc myapp.c -I./include -L./lib -lsparkplug_c_static_bundle -lssl -lcrypto -lpthread -ldl
```

## Dependencies

Only OpenSSL is required at link time (can also be statically linked):

**Alpine Linux:**
```bash
apk add openssl-dev openssl-libs-static  # For static linking
# or
apk add openssl  # For dynamic linking
```

**Ubuntu/Debian:**
```bash
apt-get install libssl-dev
```

**Fedora:**
```bash
dnf install openssl-devel
```

## Building from Source

See: https://github.com/synergy-au/sparkplug-cpp

## License

Apache-2.0
EOF

# Create build info
cat > "${PACKAGE_DIR}/BUILD_INFO.txt" << EOF
Build Information
=================
Version: ${VERSION}
Platform: ${PLATFORM}
Architecture: ${ARCH}
Build Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
Build Host: $(hostname)
Compiler: $(${CC:-cc} --version 2>/dev/null | head -1 || echo "unknown")
C Library: musl (static linking)

Library Contents:
- Sparkplug C API
- Paho MQTT C (v1.3.15)
- Protocol Buffers (v28.3)
- Abseil C++ (20240722.0)

External Dependencies:
- OpenSSL (dynamically linked or can be statically linked)
EOF

# Create tarball
echo "Creating tarball..."
cd "${BUILD_DIR}"
tar czf "${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"

echo ""
echo "SUCCESS! Package created:"
echo "  ${BUILD_DIR}/${PACKAGE_NAME}.tar.gz"
echo ""
echo "Package size:"
ls -lh "${PACKAGE_NAME}.tar.gz"
echo ""
echo "Library size:"
ls -lh "${PACKAGE_NAME}/lib/libsparkplug_c_static_bundle.a"
echo ""
echo "To test:"
echo "  cd ${BUILD_DIR}"
echo "  tar xzf ${PACKAGE_NAME}.tar.gz"
echo "  ls -lR ${PACKAGE_NAME}/"
