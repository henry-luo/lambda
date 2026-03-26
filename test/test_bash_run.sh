#!/bin/bash
# test_bash_run.sh — Run Bash transpiler integration tests
# Iterates over test/bash/*.sh scripts, runs each via lambda.exe bash,
# and compares stdout against test/bash/<name>.txt expected output.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LAMBDA_EXE="$PROJECT_DIR/lambda.exe"
TEST_DIR="$PROJECT_DIR/test/bash"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

passed=0
failed=0
skipped=0
fail_names=()

if [ ! -x "$LAMBDA_EXE" ]; then
    echo "Error: lambda.exe not found at $LAMBDA_EXE"
    echo "Run 'make build' first."
    exit 1
fi

if [ ! -d "$TEST_DIR" ]; then
    echo "Error: test/bash/ directory not found"
    exit 1
fi

echo "=============================================================="
echo "  Bash Transpiler — Integration Tests"
echo "=============================================================="
echo ""

for script in "$TEST_DIR"/*.sh; do
    name=$(basename "$script" .sh)
    expected="$TEST_DIR/$name.txt"

    if [ ! -f "$expected" ]; then
        printf "  ${YELLOW}SKIP${NC}  %-30s (no expected output file)\n" "$name"
        skipped=$((skipped + 1))
        continue
    fi

    # Run the script and capture actual output
    actual=$("$LAMBDA_EXE" bash "$script" 2>/dev/null) || true
    expected_content=$(cat "$expected")

    if [ "$actual" = "$expected_content" ]; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$name"
        passed=$((passed + 1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$name"
        failed=$((failed + 1))
        fail_names+=("$name")

        # Show diff for failed tests (first 10 lines)
        echo "        --- expected"
        echo "        +++ actual"
        diff <(echo "$expected_content") <(echo "$actual") | head -20 | sed 's/^/        /'
        echo ""
    fi
done

echo ""
echo "=============================================================="
total=$((passed + failed + skipped))
printf "  Total: %d  |  ${GREEN}Passed: %d${NC}  |  ${RED}Failed: %d${NC}  |  ${YELLOW}Skipped: %d${NC}\n" \
    "$total" "$passed" "$failed" "$skipped"
echo "=============================================================="

if [ ${#fail_names[@]} -gt 0 ]; then
    echo ""
    echo "  Failed tests:"
    for name in "${fail_names[@]}"; do
        echo "    - $name"
    done
fi

echo ""

# Exit with failure if any test failed
[ "$failed" -eq 0 ]
