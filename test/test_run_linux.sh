#!/bin/bash

# Linux Test Runner Script
# Runs cross-compiled Lambda tests using QEMU emulation or Docker
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
TEST_OUTPUT_DIR="test_output"
LINUX_EXE="lambda-linux.exe"
QEMU_USER_CMD="qemu-x86_64-static"
QEMU_SYSTEM_CMD="qemu-system-x86_64"

# Create test output directory
mkdir -p "$TEST_OUTPUT_DIR"

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

# Function to check if QEMU is available
check_qemu() {
    # First try user-mode emulation (preferred for static binaries)
    if command -v "$QEMU_USER_CMD" >/dev/null 2>&1; then
        print_status "$GREEN" "✅ QEMU user-mode found: $(which $QEMU_USER_CMD)"
        export QEMU_CMD="$QEMU_USER_CMD"
        return 0
    # Fall back to system emulation (requires more setup but available on macOS)
    elif command -v "$QEMU_SYSTEM_CMD" >/dev/null 2>&1; then
        print_status "$YELLOW" "⚠️ Only QEMU system emulation available: $(which $QEMU_SYSTEM_CMD)"
        print_status "$YELLOW" "Note: System emulation requires a Linux kernel and is complex to set up"
        print_status "$BLUE" "Falling back to Docker for testing..."
        return 1
    else
        print_status "$RED" "❌ QEMU not found. Install with: brew install qemu"
        return 1
    fi
}

# Function to check if Docker is available
check_docker() {
    if command -v docker >/dev/null 2>&1; then
        print_status "$GREEN" "✅ Docker found: $(which docker)"
        return 0
    else
        print_status "$RED" "❌ Docker not found. Install Docker Desktop."
        return 1
    fi
}

# Function to verify Linux executable
verify_linux_executable() {
    print_status "$BLUE" "🔍 Verifying Linux executable..."
    
    if [ ! -f "$LINUX_EXE" ]; then
        print_status "$RED" "❌ Linux executable not found: $LINUX_EXE"
        print_status "$YELLOW" "Building Linux executable..."
        make build-linux
        
        if [ ! -f "$LINUX_EXE" ]; then
            print_status "$RED" "❌ Failed to build Linux executable"
            return 1
        fi
    fi
    
    # Check file type
    local file_info=$(file "$LINUX_EXE")
    print_status "$BLUE" "📁 File info: $file_info"
    
    if echo "$file_info" | grep -q "ELF.*x86-64"; then
        print_status "$GREEN" "✅ Linux executable verified"
        return 0
    else
        print_status "$RED" "❌ Invalid Linux executable format"
        return 1
    fi
}

# Function to test Linux executable with QEMU
test_linux_executable() {
    print_status "$BLUE" "🧪 Testing Linux executable functionality..."
    
    # Test version command
    print_status "$BLUE" "Testing --version..."
    if $QEMU_CMD "./$LINUX_EXE" --version > "$TEST_OUTPUT_DIR/version_test.txt" 2>&1; then
        print_status "$GREEN" "✅ Version test passed"
        cat "$TEST_OUTPUT_DIR/version_test.txt"
    else
        print_status "$RED" "❌ Version test failed"
        cat "$TEST_OUTPUT_DIR/version_test.txt"
        return 1
    fi
    
    # Test help command
    print_status "$BLUE" "Testing --help..."
    if $QEMU_CMD "./$LINUX_EXE" --help > "$TEST_OUTPUT_DIR/help_test.txt" 2>&1; then
        print_status "$GREEN" "✅ Help test passed"
        head -5 "$TEST_OUTPUT_DIR/help_test.txt"
    else
        print_status "$RED" "❌ Help test failed"
        cat "$TEST_OUTPUT_DIR/help_test.txt"
        return 1
    fi
    
    # Test simple script execution
    print_status "$BLUE" "Testing simple script execution..."
    echo 'print("Hello from Linux Lambda!")' > "$TEST_OUTPUT_DIR/test_script.ls"
    
    if $QEMU_CMD "./$LINUX_EXE" "$TEST_OUTPUT_DIR/test_script.ls" > "$TEST_OUTPUT_DIR/script_test.txt" 2>&1; then
        print_status "$GREEN" "✅ Script execution test passed"
        cat "$TEST_OUTPUT_DIR/script_test.txt"
    else
        print_status "$RED" "❌ Script execution test failed"
        cat "$TEST_OUTPUT_DIR/script_test.txt"
        return 1
    fi
    
    rm -f "$TEST_OUTPUT_DIR/test_script.ls"
    return 0
}

# Function to run Catch2 tests with QEMU
run_catch2_tests_qemu() {
    print_status "$BLUE" "🧪 Running Catch2 tests with QEMU..."
    
    local test_count=0
    local passed_count=0
    local failed_count=0
    
    # Find all Catch2 test executables
    local test_executables=($(find test -name "*catch2.exe" -type f 2>/dev/null))
    
    if [ ${#test_executables[@]} -eq 0 ]; then
        print_status "$YELLOW" "⚠️ No Catch2 test executables found"
        print_status "$BLUE" "Building Linux tests..."
        make build-test-catch2-linux
        
        # Try again
        test_executables=($(find test -name "*catch2.exe" -type f 2>/dev/null))
        
        if [ ${#test_executables[@]} -eq 0 ]; then
            print_status "$RED" "❌ No Catch2 test executables found after build"
            return 1
        fi
    fi
    
    print_status "$BLUE" "Found ${#test_executables[@]} test executables"
    
    # Run each test executable
    for test_exe in "${test_executables[@]}"; do
        test_count=$((test_count + 1))
        local test_name=$(basename "$test_exe" .exe)
        local output_file="$TEST_OUTPUT_DIR/${test_name}_output.txt"
        
        print_status "$BLUE" "Running $test_name..."
        
        # Verify the test executable is a Linux binary
        local file_info=$(file "$test_exe")
        if ! echo "$file_info" | grep -q "ELF.*x86-64"; then
            print_status "$RED" "❌ $test_name is not a Linux binary: $file_info"
            failed_count=$((failed_count + 1))
            continue
        fi
        
        # Run the test with QEMU
        if $QEMU_CMD "$test_exe" --reporter=compact > "$output_file" 2>&1; then
            print_status "$GREEN" "✅ $test_name PASSED"
            passed_count=$((passed_count + 1))
            
            # Show summary
            if grep -q "test cases:" "$output_file"; then
                local summary=$(grep "test cases:" "$output_file" | tail -1)
                print_status "$BLUE" "   $summary"
            fi
        else
            print_status "$RED" "❌ $test_name FAILED"
            failed_count=$((failed_count + 1))
            
            # Show error details
            print_status "$YELLOW" "   Error output:"
            tail -10 "$output_file" | sed 's/^/   /'
        fi
    done
    
    # Print summary
    print_status "$BLUE" "📊 Test Summary:"
    print_status "$BLUE" "   Total tests: $test_count"
    print_status "$GREEN" "   Passed: $passed_count"
    print_status "$RED" "   Failed: $failed_count"
    
    if [ $failed_count -eq 0 ]; then
        print_status "$GREEN" "🎉 All tests passed!"
        return 0
    else
        print_status "$RED" "💥 Some tests failed"
        return 1
    fi
}

# Function to run tests with Docker
run_tests_docker() {
    print_status "$BLUE" "🐳 Running tests with Docker..."
    
    if [ -f "./test/docker-test-linux.sh" ]; then
        ./test/docker-test-linux.sh
    else
        print_status "$RED" "❌ Docker test script not found: ./test/docker-test-linux.sh"
        return 1
    fi
}

# Main function
main() {
    print_status "$BLUE" "🚀 Lambda Linux Test Runner"
    print_status "$BLUE" "================================"
    
    local use_docker=false
    local use_qemu=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --docker)
                use_docker=true
                shift
                ;;
            --qemu)
                use_qemu=true
                shift
                ;;
            --help|-h)
                echo "Usage: $0 [--docker] [--qemu] [--help]"
                echo ""
                echo "Options:"
                echo "  --docker    Use Docker for testing"
                echo "  --qemu      Use QEMU for testing"
                echo "  --help      Show this help message"
                echo ""
                echo "If no option is specified, the script will auto-detect the best method."
                exit 0
                ;;
            *)
                print_status "$RED" "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    # Auto-detect if no method specified
    if [ "$use_docker" = false ] && [ "$use_qemu" = false ]; then
        if check_qemu; then
            use_qemu=true
        elif check_docker; then
            use_docker=true
        else
            print_status "$RED" "❌ Neither QEMU nor Docker available"
            print_status "$YELLOW" "Install QEMU with: brew install qemu"
            print_status "$YELLOW" "Or install Docker Desktop"
            exit 1
        fi
    fi
    
    # Verify Linux executable
    if ! verify_linux_executable; then
        exit 1
    fi
    
    # Run tests based on selected method
    if [ "$use_qemu" = true ]; then
        if ! check_qemu; then
            exit 1
        fi
        
        # Test Linux executable functionality
        if ! test_linux_executable; then
            print_status "$RED" "❌ Linux executable tests failed"
            exit 1
        fi
        
        # Run Catch2 tests
        if ! run_catch2_tests_qemu; then
            exit 1
        fi
        
    elif [ "$use_docker" = true ]; then
        if ! check_docker; then
            exit 1
        fi
        
        if ! run_tests_docker; then
            exit 1
        fi
    fi
    
    print_status "$GREEN" "🎉 All Linux tests completed successfully!"
}

# Run main function with all arguments
main "$@"
