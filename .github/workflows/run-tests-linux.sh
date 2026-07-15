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

# A layout subcommand used to bypass the common process-exit snapshot path, so
# CI exercises the real command and requires every registered allocator to be gone.
echo ""
echo "--- Running Radiant memory survivor check ---"
set +e
mkdir -p temp
./lambda.exe --mem-dump=./temp/ci_mem_snapshot.json \
    layout test/layout/data/baseline/box_010_line_height.html --no-log \
    2>&1 | tee -a test_output.txt
MEM_DUMP_EXIT=${PIPESTATUS[0]}
if [ $MEM_DUMP_EXIT -eq 0 ]; then
    python3 -c 'import json, sys; data = json.load(open(sys.argv[1], encoding="utf-8")); sys.exit(0 if data.get("count") == 0 and data.get("nodes") == [] else 1)' \
        ./temp/ci_mem_snapshot.json
    MEM_DUMP_EXIT=$?
fi
set -e
MEM_DUMP_TIME=$(date +%s)
echo "Memory survivor check took $((MEM_DUMP_TIME - EDITOR4C_TIME))s (exit $MEM_DUMP_EXIT)"
if [ $TEST_EXIT -eq 0 ] && [ $MEM_DUMP_EXIT -ne 0 ]; then TEST_EXIT=$MEM_DUMP_EXIT; fi

# Summary
TOTAL_TIME=$((MEM_DUMP_TIME - START_TIME))
echo ""
echo "========================================"
echo "SUMMARY"
echo "========================================"
echo "Dependencies: $((DEPS_TIME - START_TIME))s"
echo "Build:        $((BUILD_TIME - DEPS_TIME))s"
echo "Tests:        $((TEST_TIME - BUILD_TIME))s"
echo "Stage 4C:     $((EDITOR4C_TIME - TEST_TIME))s"
echo "Memory check: $((MEM_DUMP_TIME - EDITOR4C_TIME))s"
echo "Total:        ${TOTAL_TIME}s"
echo "Test result:  $([ $TEST_EXIT -eq 0 ] && echo 'PASS' || echo 'FAIL')"
echo "========================================"

exit $TEST_EXIT
