#!/bin/bash
# Test Lambda validator through CLI

set -e  # Exit on error

echo "=========================================="
echo "Lambda Validator CLI Tests"
echo "=========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0

# Helper function to run a test
run_test() {
    local test_name="$1"
    local command="$2"
    local expected_exit="$3"
    
    echo -n "Testing: $test_name ... "
    
    if eval "$command" > /dev/null 2>&1; then
        actual_exit=0
    else
        actual_exit=$?
    fi
    
    if [ "$actual_exit" -eq "$expected_exit" ]; then
        echo -e "${GREEN}PASS${NC}"
        ((PASSED++))
    else
        echo -e "${RED}FAIL${NC} (expected exit $expected_exit, got $actual_exit)"
        ((FAILED++))
    fi
}

# Check if lambda executable exists
if [ ! -f "./lambda.exe" ]; then
    echo -e "${RED}ERROR: lambda.exe not found. Please build first with: make build${NC}"
    exit 1
fi

# Create test files
mkdir -p test/temp

# Test 1: Create valid JSON file
cat > test/temp/valid.json << 'EOF'
{
    "name": "Alice",
    "age": 30,
    "email": "alice@example.com"
}
EOF

# Test 2: Create invalid JSON file (not valid JSON)
cat > test/temp/invalid.json << 'EOF'
{
    "name": "Bob",
    "age": "not a number"
}
EOF

# Test 3: Create a simple Lambda schema
cat > test/temp/person_schema.ls << 'EOF'
type Person = {
    name: string,
    age: int,
    email: string
}
EOF

# Test 4: Create a Lambda test file
cat > test/temp/test.ls << 'EOF'
let x = 42
let y = "hello"
EOF

echo ""
echo "Running validator tests..."
echo ""

# Test: Show help
run_test "Validator help" "./lambda.exe validate --help" 0

# Test: Validate JSON requires schema (should fail without -s option)
run_test "Valid JSON parse" "./lambda.exe validate -f json test/temp/valid.json" 1

# Test: Validate non-existent file (should fail)
run_test "Non-existent file" "./lambda.exe validate test/temp/nonexistent.json" 1

# Test: Validate Lambda script (AST validation)
run_test "Lambda script validation" "./lambda.exe validate test/temp/test.ls" 0

# Test: Missing required arguments
run_test "No file specified" "./lambda.exe validate" 1

echo ""
echo "=========================================="
echo "Test Results:"
echo -e "  ${GREEN}Passed: $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "  ${RED}Failed: $FAILED${NC}"
else
    echo -e "  Failed: $FAILED"
fi
echo "=========================================="

# Cleanup
rm -f test/temp/valid.json test/temp/invalid.json test/temp/person_schema.ls test/temp/test.ls

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
