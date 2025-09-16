#!/bin/bash

# Simple script to build just Criterion with full debug output
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="linux-deps"
LOG_FILE="$SCRIPT_DIR/criterion_debug.log"
GLIBC_TOOLCHAIN_PATH="/opt/homebrew/Cellar/x86_64-unknown-linux-gnu/13.3.0"

# Clear previous log
> "$LOG_FILE"

echo "=== Building Criterion with full debug output ===" | tee -a "$LOG_FILE"
echo "Log file: $LOG_FILE" | tee -a "$LOG_FILE"

VERSION="2.4.2"
URL="https://github.com/Snaipe/Criterion/archive/v${VERSION}.tar.gz"

cd /tmp
rm -rf criterion-*

echo "=== Downloading Criterion ${VERSION} ===" | tee -a "$LOG_FILE"
curl -LO "$URL" 2>&1 | tee -a "$LOG_FILE"

echo "=== Extracting Criterion ===" | tee -a "$LOG_FILE"
tar -xzf "v${VERSION}.tar.gz" 2>&1 | tee -a "$LOG_FILE"
cd "criterion-${VERSION}"

echo "=== Creating cross-compilation file ===" | tee -a "$LOG_FILE"
# Create cross-compilation file for Meson
cat > cross-file.txt << EOF | tee -a "$LOG_FILE"
[binaries]
c = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc'
cpp = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-g++'
ar = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar'
strip = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-strip'
pkg-config = '/usr/bin/false'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

echo "=== Cross-compilation file contents ===" | tee -a "$LOG_FILE"
cat cross-file.txt | tee -a "$LOG_FILE"

echo "=== Running meson setup with full debug ===" | tee -a "$LOG_FILE"
rm -rf build
meson setup build \
    --cross-file=cross-file.txt \
    --prefix="$SCRIPT_DIR/$DEPS_DIR" \
    --buildtype=release \
    -Dtests=false \
    -Dsamples=false \
    -v 2>&1 | tee -a "$LOG_FILE"

EXIT_CODE=${PIPESTATUS[0]}
echo "=== Meson setup exit code: $EXIT_CODE ===" | tee -a "$LOG_FILE"

if [ $EXIT_CODE -ne 0 ]; then
    echo "=== MESON SETUP FAILED ===" | tee -a "$LOG_FILE"
    echo "Check $LOG_FILE for full details"
    exit 1
fi

echo "=== Running ninja build ===" | tee -a "$LOG_FILE"
cd build
ninja -v 2>&1 | tee -a "$LOG_FILE"

EXIT_CODE=${PIPESTATUS[0]}
echo "=== Ninja build exit code: $EXIT_CODE ===" | tee -a "$LOG_FILE"

if [ $EXIT_CODE -ne 0 ]; then
    echo "=== NINJA BUILD FAILED ===" | tee -a "$LOG_FILE"
    echo "Check $LOG_FILE for full details"
    exit 1
fi

echo "=== Running ninja install ===" | tee -a "$LOG_FILE"
ninja install -v 2>&1 | tee -a "$LOG_FILE"

EXIT_CODE=${PIPESTATUS[0]}
echo "=== Ninja install exit code: $EXIT_CODE ===" | tee -a "$LOG_FILE"

echo "=== CRITERION BUILD COMPLETED SUCCESSFULLY ===" | tee -a "$LOG_FILE"
echo "Full log available at: $LOG_FILE"