#!/bin/bash
# x86-64 hello world end-to-end test
# Tests that iSH can load and run a static x86-64 ELF binary

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ISH="${ISH:-../../../../../build/ish}"
FIXTURE_DIR="${SCRIPT_DIR}/../../fixtures/x86_64"

echo "=== x86-64 hello world test ==="

# Check if iSH binary exists
if [ ! -x "$ISH" ]; then
    echo "SKIP: iSH binary not found at $ISH"
    echo "Build iSH first: meson setup build && ninja -C build"
    exit 0
fi

# Check if fixture exists
if [ ! -x "$FIXTURE_DIR/hello" ]; then
    echo "SKIP: x86-64 hello fixture not built"
    echo "Build fixtures: bash $FIXTURE_DIR/build-fixtures.sh"
    exit 0
fi

# Create a minimal rootfs with the test binary
ROOTFS=$(mktemp -d)
mkdir -p "$ROOTFS/bin"
cp "$FIXTURE_DIR/hello" "$ROOTFS/bin/hello"

# Run the test
echo "Running x86-64 hello binary..."
OUTPUT=$(cd "$ROOTFS" && "$ISH" /bin/hello 2>&1)
EXIT_CODE=$?

# Check results
if [ "$EXIT_CODE" -ne 0 ]; then
    echo "FAIL: Expected exit code 0, got $EXIT_CODE"
    echo "Output: $OUTPUT"
    rm -rf "$ROOTFS"
    exit 1
fi

if [ "$OUTPUT" != "hi" ]; then
    echo "FAIL: Expected output 'hi', got '$OUTPUT'"
    rm -rf "$ROOTFS"
    exit 1
fi

echo "PASS: x86-64 hello world test passed!"
echo "  Output: $OUTPUT"
echo "  Exit code: $EXIT_CODE"

# Cleanup
rm -rf "$ROOTFS"
exit 0
