#!/usr/bin/env bash
# Run clang-tidy on all C++ source files

set -e

# Detect clang-tidy binary
if [ -f /opt/homebrew/opt/llvm/bin/clang-tidy ]; then
    CLANG_TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy
elif command -v clang-tidy-18 &> /dev/null; then
    CLANG_TIDY=clang-tidy-18
elif command -v clang-tidy &> /dev/null; then
    CLANG_TIDY=clang-tidy
else
    echo "Error: clang-tidy not found!"
    echo "Install it with: brew install llvm (macOS) or apt install clang-tidy-18 (Linux)"
    exit 1
fi

echo "Using: $CLANG_TIDY"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Check for build directory with compile_commands.json
BUILD_DIR=""
if [ -f "build/compile_commands.json" ]; then
    BUILD_DIR="build"
elif [ -f "build-release/compile_commands.json" ]; then
    BUILD_DIR="build-release"
else
    echo "Error: compile_commands.json not found!"
    echo "Please build the project first:"
    echo "  cmake --preset default"
    echo "  cmake --build build"
    exit 1
fi

echo "Using build directory: $BUILD_DIR"

# Parse arguments
FIX_MODE=false
QUIET_MODE=false
FILES_TO_CHECK=()

for arg in "$@"; do
    case $arg in
        --fix)
            FIX_MODE=true
            shift
            ;;
        --quiet)
            QUIET_MODE=true
            shift
            ;;
        *)
            FILES_TO_CHECK+=("$arg")
            ;;
    esac
done

# Find all C++ source files if none specified
# Only check .cpp files - headers will be analyzed when included
if [ ${#FILES_TO_CHECK[@]} -eq 0 ]; then
    mapfile -t FILES_TO_CHECK < <(find src tests examples \
        -name "*.cpp" \
        -not -path "*/build/*" \
        -not -path "*/build-*/*")
fi

if [ ${#FILES_TO_CHECK[@]} -eq 0 ]; then
    echo "No C++ files found!"
    exit 1
fi

FILE_COUNT=${#FILES_TO_CHECK[@]}
echo "Checking $FILE_COUNT C/C++ files"

# Build clang-tidy command
# Only report issues from project headers (not third-party libraries)
# Add SDK path for macOS system headers
SDK_PATH=""
if command -v xcrun &> /dev/null; then
    SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || echo "")
fi

TIDY_CMD="$CLANG_TIDY -p $BUILD_DIR --header-filter='(include/sparkplug|src)/.*'"
if [ -n "$SDK_PATH" ]; then
    TIDY_CMD="$TIDY_CMD --extra-arg=-isysroot$SDK_PATH"
fi

if [ "$FIX_MODE" = true ]; then
    TIDY_CMD="$TIDY_CMD --fix"
    echo "Running in fix mode (will modify files)..."
fi

if [ "$QUIET_MODE" = true ]; then
    TIDY_CMD="$TIDY_CMD --quiet"
fi

# Run clang-tidy
FAILED=0
for file in "${FILES_TO_CHECK[@]}"; do
    if ! $TIDY_CMD "$file"; then
        FAILED=$((FAILED + 1))
    fi
done

if [ $FAILED -eq 0 ]; then
    echo "[OK] All files passed clang-tidy checks"
else
    echo "[NOK] $FAILED file(s) have clang-tidy warnings"
    if [ "$FIX_MODE" = false ]; then
        echo "Run './scripts/tidy.sh --fix' to automatically fix some issues."
    fi
    exit 1
fi
