# Packaging Guide for Sparkplug C Bindings

This guide explains how to create and use binary packages of the Sparkplug C bindings for distribution.

## Automated GitHub Releases (Recommended)

Every tagged release automatically builds and publishes prebuilt binaries for multiple platforms via GitHub Actions.

### Available Release Artifacts

Download from [GitHub Releases](https://github.com/synergy-au/sparkplug-cpp/releases):

**Static Libraries (No Dependencies):**
- `sparkplug-c-{version}-linux-musl-x86_64-static.tar.gz` - **Fully static C API bundle**
  - Includes: Paho MQTT, Protobuf, Abseil
  - Only requires: OpenSSL (can be static too)
  - Ideal for: Alpine Linux, containers, sparkplug-rs static builds

**Dynamic Libraries (Requires System Dependencies):**
- `sparkplug-c-{version}-ubuntu24.04-x86_64.tar.gz` - Ubuntu 24.04 LTS
- `sparkplug-c-{version}-fedora42-x86_64.tar.gz` - Fedora 42+
- `sparkplug-cpp-{version}-ubuntu24.04-x86_64.tar.gz` - C++ API (Ubuntu)
- `sparkplug-cpp-{version}-fedora42-x86_64.tar.gz` - C++ API (Fedora)

### Quick Start with Prebuilt Releases

```bash
# Download static bundle (works everywhere)
wget https://github.com/synergy-au/sparkplug-cpp/releases/download/v1.0.0/sparkplug-c-1.0.0-linux-musl-x86_64-static.tar.gz
tar xzf sparkplug-c-1.0.0-linux-musl-x86_64-static.tar.gz
cd sparkplug-c-1.0.0-linux-musl-x86_64-static

# Use directly or install
sudo cp lib/libsparkplug_c_static_bundle.a /usr/local/lib/
sudo cp include/sparkplug_c.h /usr/local/include/
```

### Using with sparkplug-rs

The static musl bundle is perfect for sparkplug-rs - no need to clone or build sparkplug-cpp!

**Update your build.rs:**

```rust
use std::env;
use std::path::PathBuf;

const SPARKPLUG_VERSION: &str = "1.0.0";

fn main() {
    // Check if user provided custom path
    if let Ok(lib_dir) = env::var("SPARKPLUG_LIB_DIR") {
        println!("cargo:rustc-link-search=native={}", lib_dir);
        println!("cargo:rustc-link-lib=static=sparkplug_c_static_bundle");
        link_openssl();
        return;
    }

    // Download from GitHub releases (implement download_prebuilt)
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let lib_dir = download_prebuilt(&out_dir);

    println!("cargo:rustc-link-search=native={}", lib_dir.display());
    println!("cargo:rustc-link-lib=static=sparkplug_c_static_bundle");
    link_openssl();
}

fn link_openssl() {
    println!("cargo:rustc-link-lib=ssl");
    println!("cargo:rustc-link-lib=crypto");
}

fn download_prebuilt(out_dir: &Path) -> PathBuf {
    let target = env::var("TARGET").unwrap();
    let package = if target.contains("musl") {
        format!("sparkplug-c-{}-linux-musl-x86_64-static", SPARKPLUG_VERSION)
    } else {
        format!("sparkplug-c-{}-ubuntu24.04-x86_64", SPARKPLUG_VERSION)
    };

    let lib_dir = out_dir.join(&package).join("lib");
    if lib_dir.exists() {
        return lib_dir;
    }

    // Download from GitHub releases
    let url = format!(
        "https://github.com/synergy-au/sparkplug-cpp/releases/download/v{}/{}.tar.gz",
        SPARKPLUG_VERSION, package
    );

    println!("cargo:warning=Downloading sparkplug-cpp from {}", url);

    // Use reqwest + tar or curl + tar to download and extract
    // ... implementation ...

    lib_dir
}
```

**Or use environment variable for local testing:**

```bash
# Download and extract once
wget https://github.com/synergy-au/sparkplug-cpp/releases/download/v1.0.0/sparkplug-c-1.0.0-linux-musl-x86_64-static.tar.gz
tar xzf sparkplug-c-1.0.0-linux-musl-x86_64-static.tar.gz

# Set path
export SPARKPLUG_LIB_DIR=$(pwd)/sparkplug-c-1.0.0-linux-musl-x86_64-static/lib

# Build your Rust project
cd ~/synergy/sparkplug-rs
cargo build --release
```

## Manual Package Builds

If you need to build packages manually instead of using GitHub releases:

### Overview

The Sparkplug C bindings (`libsparkplug_c`) can be packaged in multiple formats:

1. **Static musl bundle (.tar.gz)** - Fully static, no dependencies
2. **Debian Package (.deb)** - For Ubuntu/Debian systems
3. **RPM Package** - For Fedora/RHEL systems
4. **Tarball (.tar.gz)** - Dynamic libraries with dependencies

These packages are useful for:
- Distributing prebuilt binaries to avoid compilation
- Using with the Rust `sparkplug-rs` wrapper without building from source
- Deploying to production systems (e.g., `csipaus-sparkplug-client`)

## Quick Start

### Build Static musl Bundle (Recommended for sparkplug-rs)

```bash
# Requires Alpine Linux or Docker
docker run --rm -v $(pwd):/src -w /src alpine:latest sh -c '
  apk add --no-cache build-base cmake git bash linux-headers \
    openssl-dev openssl-libs-static zlib-static samurai
  ./scripts/build_static_musl.sh
'

# Output: build-static-musl/sparkplug-c-*-linux-musl-x86_64-static.tar.gz
```

This creates a fully static library with all dependencies bundled (except OpenSSL).

### Build for Current Platform

```bash
# Generic release tarball (dynamic libraries)
./scripts/build_release_package.sh

# Output: build-release-package/sparkplug-cpp-c-bindings-*.tar.gz
```

### Build Ubuntu 24.04 Package

```bash
# On Ubuntu 24.04 (native)
./scripts/build_ubuntu24_package.sh

# Or using Docker (from any platform)
docker build -f packaging/Dockerfile.ubuntu24 -t sparkplug-builder-ubuntu24 .
docker run --rm -v $(pwd):/workspace -w /workspace sparkplug-builder-ubuntu24 \
    ./scripts/build_ubuntu24_package.sh

# Output: build-ubuntu24/libsparkplug-c-*.deb
#         build-ubuntu24/libsparkplug-c-*.tar.gz
```

## Package Formats

### 1. Tarball Package (.tar.gz)

**Use case:** Cross-platform distribution, manual installation

**Build:**
```bash
./scripts/build_release_package.sh
```

**Install:**
```bash
tar xzf sparkplug-cpp-c-bindings-*.tar.gz
cd sparkplug-cpp-c-bindings-*
sudo cp -r lib/* /usr/local/lib/
sudo cp -r include/* /usr/local/include/
sudo ldconfig  # Linux only
```

**Contents:**
- `lib/` - Shared libraries (libsparkplug_c.so/dylib)
- `include/` - C header files
- `README.md` - Usage instructions
- `BUILD_INFO.txt` - Build metadata

### 2. Debian Package (.deb)

**Use case:** Ubuntu/Debian package management integration

**Build:**
```bash
./scripts/build_ubuntu24_package.sh

# Or manually with CPack
cmake --preset release
cmake --build build-release
cd build-release
cpack -G DEB
```

**Install:**
```bash
sudo dpkg -i libsparkplug-c-*.deb
sudo apt-get install -f  # Resolve dependencies
```

**Remove:**
```bash
sudo apt-get remove libsparkplug-c-dev
```

### 3. RPM Package

**Build on Fedora 40+:**
```bash
cmake --preset release
cmake --build build-release
cd build-release
cpack -G RPM
```

**Install:**
```bash
sudo dnf install libsparkplug-c-*.rpm
```

## Dependencies

### Runtime Dependencies

The packages require these runtime dependencies:

#### Ubuntu 24.04
```bash
sudo apt-get install \
    libprotobuf33 \
    libabsl20230802 \
    libpaho-mqtt-c1.3 \
    libssl3
```

#### Fedora 40+
```bash
sudo dnf install \
    protobuf \
    abseil-cpp \
    paho-c \
    openssl
```

#### macOS (Homebrew)
```bash
brew install \
    protobuf \
    abseil \
    paho-mqtt-c \
    openssl@3
```

### Build Dependencies

Additional packages needed to **build** from source:

#### Ubuntu 24.04
```bash
sudo apt-get install \
    build-essential \
    cmake \
    git \
    libprotobuf-dev \
    protobuf-compiler \
    libabsl-dev \
    libpaho-mqtt-dev \
    libssl-dev \
    clang-18 \
    libc++-18-dev
```

## Using Prebuilt Packages with sparkplug-rs

The Rust `sparkplug-rs` wrapper can use prebuilt packages instead of building from source.

### Option 1: System-wide Installation

Install the package system-wide, then build normally:

```bash
# Install .deb package
sudo dpkg -i libsparkplug-c-*.deb

# Build Rust project
cd ~/synergy/sparkplug-rs
cargo build
```

Modify `sparkplug-rs/build.rs` to check for system libraries first:

```rust
// Check if library is already installed
if pkg_config::probe_library("sparkplug_c").is_ok() {
    println!("Using system-installed sparkplug_c library");
    return;
}

// Otherwise, clone and build from source...
```

### Option 2: Custom Library Path

Extract the package and set environment variables:

```bash
# Extract package
tar xzf sparkplug-cpp-c-bindings-*.tar.gz
export SPARKPLUG_LIB_DIR=$(pwd)/sparkplug-cpp-c-bindings-*/lib
export SPARKPLUG_INCLUDE_DIR=$(pwd)/sparkplug-cpp-c-bindings-*/include

# Build Rust project
cd ~/synergy/sparkplug-rs
cargo build
```

Modify `sparkplug-rs/build.rs`:

```rust
// Use environment variable if set
if let Ok(lib_dir) = env::var("SPARKPLUG_LIB_DIR") {
    println!("cargo:rustc-link-search=native={}", lib_dir);
    println!("cargo:rustc-link-lib=dylib=sparkplug_c");
    return;
}

// Otherwise, clone and build from source...
```

### Option 3: Vendored Packages

Include prebuilt binaries in your Rust project:

```bash
cd ~/synergy/csipaus-sparkplug-client
mkdir -p vendor/sparkplug-cpp/{lib,include}

# Copy from extracted package
cp -r /path/to/package/lib/* vendor/sparkplug-cpp/lib/
cp -r /path/to/package/include/* vendor/sparkplug-cpp/include/
```

Update `.cargo/config.toml`:

```toml
[env]
SPARKPLUG_LIB_DIR = { value = "vendor/sparkplug-cpp/lib", relative = true }
SPARKPLUG_INCLUDE_DIR = { value = "vendor/sparkplug-cpp/include", relative = true }
```

## Docker-based Cross-Platform Builds

### Ubuntu 24.04 (from macOS/Windows)

```bash
# Build the builder image
docker build -f packaging/Dockerfile.ubuntu24 -t sparkplug-builder-ubuntu24 .

# Run the build
docker run --rm -v $(pwd):/workspace -w /workspace \
    sparkplug-builder-ubuntu24 \
    ./scripts/build_ubuntu24_package.sh

# Extract the package
cp build-ubuntu24/*.deb .
```

### Create Multi-Platform Packages

```bash
# Build for multiple platforms
for platform in ubuntu24 fedora40; do
    docker build -f packaging/Dockerfile.$platform -t sparkplug-builder-$platform .
    docker run --rm -v $(pwd):/workspace -w /workspace \
        sparkplug-builder-$platform \
        ./scripts/build_${platform}_package.sh
done
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build Packages

on: [push, pull_request]

jobs:
  build-ubuntu24:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake clang-18 libprotobuf-dev libabsl-dev libpaho-mqtt-dev
      - name: Build package
        run: ./scripts/build_ubuntu24_package.sh
      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: ubuntu24-packages
          path: build-ubuntu24/*.deb
```

## Versioning

Package versions follow semantic versioning (MAJOR.MINOR.PATCH):

- Set in `cmake/Package.cmake`:
  ```cmake
  set(CPACK_PACKAGE_VERSION_MAJOR "0")
  set(CPACK_PACKAGE_VERSION_MINOR "17")
  set(CPACK_PACKAGE_VERSION_PATCH "1")
  ```

- Or via environment variable:
  ```bash
  VERSION=0.17.2 ./scripts/build_release_package.sh
  ```

## Troubleshooting

### Missing Dependencies

If `dpkg -i` fails with missing dependencies:

```bash
sudo apt-get install -f
```

### Wrong ABI Version

Ensure the package was built with the same C++ ABI version:

```bash
# Check library dependencies
ldd /usr/local/lib/libsparkplug_c.so

# Check C++ ABI
readelf -d /usr/local/lib/libsparkplug_c.so | grep NEEDED
```

### Runtime Errors

Set `LD_LIBRARY_PATH` if the library is not in a standard location:

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

## Distribution

Recommended distribution methods:

1. **GitHub Releases** - Attach packages as release artifacts
2. **Package Repository** - Host on Artifactory, Cloudsmith, or similar
3. **Container Registry** - Package in Docker images
4. **Direct Distribution** - Share tarball/deb files directly

Example GitHub Release workflow:

```yaml
- name: Create Release
  uses: softprops/action-gh-release@v1
  with:
    files: |
      build-ubuntu24/*.deb
      build-release-package/*.tar.gz
```

## See Also

- [Building from Source](../README.md#building)
- [sparkplug-rs Integration](https://github.com/synergy-au/sparkplug-rs)
- [CPack Documentation](https://cmake.org/cmake/help/latest/module/CPack.html)
