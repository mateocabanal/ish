#!/bin/bash
# Build script for x86-64 test fixtures
# Requires: x86_64-linux-musl-gcc or clang with x86_64 support

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CC="${X86_64_CC:-x86_64-linux-musl-gcc}"
CFLAGS="-nostdlib -static -nostartfiles"

echo "Building x86-64 test fixtures..."

# Check if compiler is available
if ! command -v "$CC" &> /dev/null; then
    echo "WARNING: $CC not found. Trying clang..."
    CC="clang"
    CFLAGS="--target=x86_64-linux-musl -nostdlib -static -nostartfiles"
    if ! command -v "$CC" &> /dev/null; then
        echo "ERROR: No x86-64 cross compiler found."
        echo "Install x86_64-linux-musl-gcc or set X86_64_CC environment variable."
        echo "Skipping x86-64 fixture build."
        exit 0
    fi
fi

echo "Using compiler: $CC"

# Build static hello world
echo "  Building hello.S..."
$CC $CFLAGS "$SCRIPT_DIR/hello.S" -o "$SCRIPT_DIR/hello"
echo "  Created: $SCRIPT_DIR/hello"

# Verify the binary is x86-64
if command -v file &> /dev/null; then
    file "$SCRIPT_DIR/hello"
fi

echo "Done building x86-64 fixtures."
