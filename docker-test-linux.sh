#!/bin/bash

# Run Linux Catch2 tests in Docker container
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

echo "=== Docker Linux Catch2 Test Runner ==="
echo "Project root: $PROJECT_ROOT"

# Check if Docker is running
echo "Checking Docker status..."
if ! docker info >/dev/null 2>&1; then
    echo "❌ Docker is not running or not accessible."
    echo "Please start Docker Desktop and try again."
    exit 1
fi
echo "✓ Docker is running"
echo ""

# Check if test executables exist
TEST_DIR="$PROJECT_ROOT/test"
TEST_EXECUTABLES=(
    "test_strbuf_catch2.exe"
    "test_stringbuf_catch2.exe"
    "test_strview_catch2.exe"
    "test_variable_pool_catch2.exe"
    "test_num_stack_catch2.exe"
    "test_datetime_catch2.exe"
    "test_url_catch2.exe"
    "test_url_extra_catch2.exe"
)

echo "Checking for test executables..."
MISSING_TESTS=()
for test_exe in "${TEST_EXECUTABLES[@]}"; do
    if [ ! -f "$TEST_DIR/$test_exe" ]; then
        MISSING_TESTS+=("$test_exe")
    else
        echo "✓ Found $test_exe"
    fi
done

if [ ${#MISSING_TESTS[@]} -gt 0 ]; then
    echo "❌ Missing test executables:"
    for missing in "${MISSING_TESTS[@]}"; do
        echo "   - $missing"
    done
    echo ""
    echo "Please run 'make build-test-catch2-linux' first to build the test executables."
    exit 1
fi

echo "All test executables found!"
echo ""

# Build Docker image if it doesn't exist
IMAGE_NAME="lambda-linux-test-runner"
echo "Building Docker image..."
docker build -f Dockerfile.test-linux -t "$IMAGE_NAME" .

echo "Running tests in Docker container..."
echo ""

# Create a temporary script to run all tests
TEMP_SCRIPT=$(mktemp)
cat > "$TEMP_SCRIPT" << 'EOF'
#!/bin/bash

echo "=== Running Lambda Linux Catch2 Tests ==="
echo "Environment: $(uname -a)"
echo "Container user: $(whoami)"
echo ""

cd /workspace/test

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

declare -A TEST_RESULTS

# Test executables to run
TESTS=(
    "test_strbuf_catch2.exe"
    "test_stringbuf_catch2.exe" 
    "test_strview_catch2.exe"
    "test_variable_pool_catch2.exe"
    "test_num_stack_catch2.exe"
    "test_datetime_catch2.exe"
    "test_url_catch2.exe"
    "test_url_extra_catch2.exe"
)

for test_exe in "${TESTS[@]}"; do
    echo "----------------------------------------"
    echo "Running: $test_exe"
    echo "----------------------------------------"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ -x "$test_exe" ]; then
        # Run the test and capture output and exit code
        if ./"$test_exe" --reporter=console; then
            echo "✅ PASSED: $test_exe"
            TEST_RESULTS["$test_exe"]="PASSED"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo "❌ FAILED: $test_exe"
            TEST_RESULTS["$test_exe"]="FAILED"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo "❌ NOT EXECUTABLE: $test_exe"
        TEST_RESULTS["$test_exe"]="NOT_EXECUTABLE"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    echo ""
done

echo "========================================"
echo "           TEST SUMMARY"
echo "========================================"
echo "Total tests:  $TOTAL_TESTS"
echo "Passed tests: $PASSED_TESTS"
echo "Failed tests: $FAILED_TESTS"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo "🎉 All tests passed!"
    exit 0
else
    echo "💥 Some tests failed:"
    for test_exe in "${TESTS[@]}"; do
        result="${TEST_RESULTS[$test_exe]}"
        if [ "$result" != "PASSED" ]; then
            echo "   - $test_exe: $result"
        fi
    done
    exit 1
fi
EOF

chmod +x "$TEMP_SCRIPT"

# Run Docker container with the test script
docker run --rm \
    -v "$PROJECT_ROOT/test:/workspace/test:ro" \
    -v "$TEMP_SCRIPT:/workspace/run_tests.sh:ro" \
    "$IMAGE_NAME" \
    /workspace/run_tests.sh

# Clean up
rm -f "$TEMP_SCRIPT"

echo ""
echo "Docker test execution completed."