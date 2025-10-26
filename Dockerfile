# Dockerfile for verifying CI process locally
# Mirrors the GitHub Actions workflow: format -> tidy -> build -> test
FROM fedora:42

# Install all dependencies (build + test)
RUN dnf install -y \
    gcc-c++ \
    clang \
    clang-tools-extra \
    cmake \
    ninja-build \
    git \
    protobuf-devel \
    abseil-cpp-devel \
    paho-c-devel \
    openssl-devel \
    mosquitto \
    clang-format \
    && dnf clean all

WORKDIR /workspace

# Copy source code
COPY . .

# Set compiler environment
ENV CC=clang
ENV CXX=clang++

# Run CI verification steps in order
RUN echo "=== Step 1: Format Check ===" && \
    ./scripts/format.sh --check && \
    echo "=== Step 2: Configure CMake ===" && \
    cmake --preset default && \
    echo "=== Step 3: Build (for compile_commands.json) ===" && \
    cmake --build build -j$(nproc) && \
    echo "=== Step 4: Tidy Check ===" && \
    ./scripts/tidy.sh --quiet && \
    echo "=== Step 5: Start Mosquitto Broker ===" && \
    echo "listener 1883" > /tmp/mosquitto.conf && \
    echo "allow_anonymous true" >> /tmp/mosquitto.conf && \
    mosquitto -c /tmp/mosquitto.conf -d && \
    sleep 2 && \
    timeout 1 mosquitto_sub -h localhost -t test || true && \
    echo "=== Step 6: Run Tests ===" && \
    ctest --test-dir build --output-on-failure && \
    echo "=== All CI steps passed! ==="

# Default command: print success message
CMD ["echo", "CI verification completed successfully. All checks passed!"]
