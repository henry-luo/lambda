#!/bin/bash
# Linux test runner for CI/CD
# Executed inside the Docker container
set -eo pipefail

echo "========================================"
echo "Lambda CI — Linux Build & Test"
echo "Date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
echo "========================================"

START_TIME=$(date +%s)

# Ensure test_output.txt exists (even if build fails)
touch test_output.txt

# Run full dependency setup (builds from-source deps like MIR, RE2, ThorVG, etc.)
echo ""
echo "--- Setting up dependencies ---"
bash setup-linux-deps.sh
DEPS_TIME=$(date +%s)
echo "Dependencies setup took $((DEPS_TIME - START_TIME))s"

# Install npm deps (jsdom for test comparators)
if [ -f package.json ]; then
    echo ""
    echo "--- Installing npm dependencies ---"
    npm install
fi

# Build
echo ""
echo "--- Building Lambda ---"
make build
BUILD_TIME=$(date +%s)
echo "Build took $((BUILD_TIME - DEPS_TIME))s"

# Test (disable errexit so we capture the exit code)
echo ""
echo "--- Running Tests ---"
set +e
make test 2>&1 | tee test_output.txt
TEST_EXIT=${PIPESTATUS[0]}
set -e
TEST_TIME=$(date +%s)
echo "Tests took $((TEST_TIME - BUILD_TIME))s"

# Summary
TOTAL_TIME=$((TEST_TIME - START_TIME))
echo ""
echo "========================================"
echo "SUMMARY"
echo "========================================"
echo "Dependencies: $((DEPS_TIME - START_TIME))s"
echo "Build:        $((BUILD_TIME - DEPS_TIME))s"
echo "Tests:        $((TEST_TIME - BUILD_TIME))s"
echo "Total:        ${TOTAL_TIME}s"
echo "Test result:  $([ $TEST_EXIT -eq 0 ] && echo 'PASS' || echo 'FAIL')"
echo "========================================"

exit $TEST_EXIT
