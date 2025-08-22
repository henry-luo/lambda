#!/bin/bash

# Parse Criterion JSON test results and extract failed test names
# Usage: ./parse_test_results.sh <json_file>

if [ $# -eq 0 ]; then
    echo "Usage: $0 <json_file>"
    exit 1
fi

JSON_FILE="$1"

if [ ! -f "$JSON_FILE" ]; then
    echo "Error: JSON file '$JSON_FILE' not found"
    exit 1
fi

# Check if jq is available
if ! command -v jq &> /dev/null; then
    echo "Error: jq is required but not installed"
    exit 1
fi

# Extract test summary
echo "=== TEST SUMMARY ==="
TOTAL_PASSED=$(jq -r '.passed' "$JSON_FILE")
TOTAL_FAILED=$(jq -r '.failed' "$JSON_FILE")
TOTAL_ERRORED=$(jq -r '.errored' "$JSON_FILE")
TOTAL_SKIPPED=$(jq -r '.skipped' "$JSON_FILE")

echo "Passed: $TOTAL_PASSED"
echo "Failed: $TOTAL_FAILED"
echo "Errored: $TOTAL_ERRORED"
echo "Skipped: $TOTAL_SKIPPED"

# Extract failed test names
echo ""
echo "=== FAILED TESTS ==="
FAILED_TESTS=$(jq -r '.test_suites[].tests[] | select(.status == "FAILED") | .name' "$JSON_FILE")

if [ -z "$FAILED_TESTS" ]; then
    echo "No failed tests found."
else
    echo "$FAILED_TESTS"
fi

# Extract errored test names if any
echo ""
echo "=== ERRORED TESTS ==="
ERRORED_TESTS=$(jq -r '.test_suites[].tests[] | select(.status == "ERRORED") | .name' "$JSON_FILE")

if [ -z "$ERRORED_TESTS" ]; then
    echo "No errored tests found."
else
    echo "$ERRORED_TESTS"
fi

exit 0
