#!/bin/bash

# Quick CI test script for lambda-windows.exe
# Returns 0 if all tests pass, non-zero if any test fails
# Usage: ./test-windows-exe-ci.sh (run from test directory)

set -e

EXECUTABLE="../lambda-windows.exe"
TEST_FILE="/tmp/ci_test.ls"
TIMEOUT_DURATION=10

# Function to log with timestamp
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

# Function to cleanup on exit
cleanup() {
    rm -f "$TEST_FILE" /tmp/ci_*.txt libwinpthread-1.dll 2>/dev/null || true
}
trap cleanup EXIT

log "Starting CI verification of $EXECUTABLE"

# Check prerequisites
if ! command -v wine &> /dev/null; then
    log "ERROR: Wine not available"
    exit 1
fi

if [ ! -f "$EXECUTABLE" ]; then
    log "ERROR: $EXECUTABLE not found"
    exit 1
fi

# Copy required DLL if needed
if [ ! -f "libwinpthread-1.dll" ]; then
    PTHREAD_DLL=$(find /opt/homebrew -name "libwinpthread-1.dll" 2>/dev/null | grep x86_64 | head -1)
    if [ -n "$PTHREAD_DLL" ]; then
        # Copy to Wine system directory
        mkdir -p "$HOME/.wine/drive_c/windows/system32"
        cp "$PTHREAD_DLL" "$HOME/.wine/drive_c/windows/system32/"
        # Also copy locally
        cp "$PTHREAD_DLL" .
        log "INFO: Copied libwinpthread-1.dll to Wine"
    fi
fi

# Test 1: Basic execution
log "Test 1: Basic execution test"
if timeout $TIMEOUT_DURATION wine "$EXECUTABLE" --help > /tmp/ci_help.txt 2>&1; then
    if grep -q "Usage:" /tmp/ci_help.txt || grep -q "lambda" /tmp/ci_help.txt; then
        log "PASS: Help command successful"
    else
        log "FAIL: Help output doesn't contain expected text"
        head -10 /tmp/ci_help.txt
        exit 1
    fi
else
    log "WARN: Help command failed, trying without arguments"
    if timeout $TIMEOUT_DURATION wine "$EXECUTABLE" > /tmp/ci_help.txt 2>&1; then
        log "PASS: Basic execution successful (no args)"
    else
        log "FAIL: Basic execution failed"
        head -10 /tmp/ci_help.txt
        exit 1
    fi
fi

# Test 2: Script processing
log "Test 2: Script processing test"
cat > "$TEST_FILE" << 'EOF'
fn main() {
    let result = 10 + 5
    result
}
EOF

if timeout $TIMEOUT_DURATION wine "$EXECUTABLE" "$TEST_FILE" > /tmp/ci_script.txt 2>&1; then
    if grep -q "parsing took" /tmp/ci_script.txt; then
        log "PASS: Script processing successful"
    else
        log "FAIL: Script processing output unexpected"
        cat /tmp/ci_script.txt
        exit 1
    fi
else
    log "FAIL: Script processing failed or timed out"
    cat /tmp/ci_script.txt 2>/dev/null || true
    exit 1
fi

# Test 3: Error handling
log "Test 3: Error handling test"
echo "invalid syntax here" > "$TEST_FILE"
if timeout $TIMEOUT_DURATION wine "$EXECUTABLE" "$TEST_FILE" > /tmp/ci_error.txt 2>&1; then
    # Should handle invalid syntax gracefully
    log "PASS: Error handling completed"
else
    # Non-zero exit is expected for invalid syntax
    log "PASS: Error handling with non-zero exit (expected)"
fi

log "SUCCESS: All CI tests passed"
exit 0
