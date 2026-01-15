#!/usr/bin/env bash
# Format all C++ source files using clang-format

set -e

# Detect clang-format binary (prefer clang-format-18 to match CI)
if [ -n "$CLANG_FORMAT" ]; then
    # Use environment variable if set
    :
elif command -v clang-format-18 &> /dev/null; then
    CLANG_FORMAT=clang-format-18
elif command -v clang-format-mp-18 &> /dev/null; then
    # MacPorts clang-18
    CLANG_FORMAT=clang-format-mp-18
elif [ -f /opt/local/bin/clang-format-mp-18 ]; then
    CLANG_FORMAT=/opt/local/bin/clang-format-mp-18
elif [ -f /opt/homebrew/opt/llvm@18/bin/clang-format ]; then
    CLANG_FORMAT=/opt/homebrew/opt/llvm@18/bin/clang-format
elif command -v /opt/homebrew/bin/clang-format &> /dev/null; then
    CLANG_FORMAT=/opt/homebrew/bin/clang-format
elif command -v clang-format &> /dev/null; then
    CLANG_FORMAT=clang-format
else
    echo "Error: clang-format not found!"
    echo "Install it with:"
    echo "  macOS (MacPorts): sudo port install clang-18"
    echo "  macOS (Homebrew): brew install llvm@18"
    echo "  Linux (Ubuntu):   apt install clang-format-18"
    exit 1
fi

echo "Using: $CLANG_FORMAT"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Check mode (--check flag for CI/dry-run)
CHECK_MODE=false
if [ "$1" = "--check" ]; then
    CHECK_MODE=true
    echo "Running in check mode (dry-run)..."
fi

# Find all C++ and C files
FILES=$(find src include tests examples \
    \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.c" \) \
    -not -path "*/build/*" \
    -not -path "*/build-*/*" \
    -not -path "*/proto/*")

if [ -z "$FILES" ]; then
    echo "No C++ files found!"
    exit 1
fi

FILE_COUNT=$(echo "$FILES" | wc -l | tr -d ' ')
echo "Found $FILE_COUNT C/C++ files"

if [ "$CHECK_MODE" = true ]; then
    # Check mode - verify formatting without modifying files
    echo "$FILES" | xargs $CLANG_FORMAT --dry-run --Werror
    if [ $? -eq 0 ]; then
        echo "[OK] All files are properly formatted"
    else
        echo "[NOK] Formatting issues found! Run './scripts/format.sh' to fix."
        exit 1
    fi
else
    # Format mode - modify files in place
    echo "$FILES" | xargs $CLANG_FORMAT -i
    echo "[OK] Formatted $FILE_COUNT files"
fi
