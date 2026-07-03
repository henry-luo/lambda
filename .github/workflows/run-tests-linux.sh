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

# Stage 4C editor conformance — Phase A breadth + vitest/jsdom parity report
# (headless: node + esbuild + lambda.exe js + jsdom) and Phase B depth
# (lambda.exe view --headless + event_sim). `make editor-4c` = parity + view.
echo ""
echo "--- Running Stage 4C editor conformance (make editor-4c) ---"
set +e
( cd test/editor-js && npm install ) >> test_output.txt 2>&1
make editor-4c 2>&1 | tee -a test_output.txt
EDITOR4C_EXIT=${PIPESTATUS[0]}
set -e
EDITOR4C_TIME=$(date +%s)
echo "Stage 4C took $((EDITOR4C_TIME - TEST_TIME))s (exit $EDITOR4C_EXIT)"
# Fold Stage 4C into the overall result (only downgrade a passing run).
if [ $TEST_EXIT -eq 0 ] && [ $EDITOR4C_EXIT -ne 0 ]; then TEST_EXIT=$EDITOR4C_EXIT; fi

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
