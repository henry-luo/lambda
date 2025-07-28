#!/bin/bash

# Memory Leak Detection Test Suite
# Uses Valgrind and AddressSanitizer to detect memory issues

set -e

echo "================================================"
echo "     Lambda Memory Leak Detection Suite        "
echo "================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}$1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

# Check if Valgrind is available
check_valgrind() {
    if command -v valgrind >/dev/null 2>&1; then
        print_status "Found Valgrind for memory leak detection"
        return 0
    else
        print_warning "Valgrind not found. Install with: brew install valgrind"
        return 1
    fi
}

# Compile with AddressSanitizer
compile_with_asan() {
    print_status "🔧 Compiling with AddressSanitizer..."
    
    local test_sources=(
        "test_strbuf.c"
        "test_strview.c"
        "test_variable_pool.c"
        "test_num_stack.c"
        "test_mime_detect.c"
        "test_mir.c"
        "test_validator.c"
    )
    
    local asan_flags="-fsanitize=address -fno-omit-frame-pointer -g"
    local criterion_flags="-lcriterion"
    
    for test_source in "${test_sources[@]}"; do
        local test_binary="test_asan_${test_source%%.c}.exe"
        local deps=""
        
        case "$test_source" in
            "test_strbuf.c")
                deps="../lib/strbuf.c ../lib/mem-pool/src/variable.c ../lib/mem-pool/src/buffer.c ../lib/mem-pool/src/utils.c -I../lib/mem-pool/include"
                ;;
            "test_strview.c")
                deps="../lib/strview.c"
                ;;
            "test_variable_pool.c")
                deps="../lib/mem-pool/src/variable.c ../lib/mem-pool/src/buffer.c ../lib/mem-pool/src/utils.c -I../lib/mem-pool/include"
                ;;
            "test_num_stack.c")
                deps="../lib/num_stack.c"
                ;;
            "test_mime_detect.c")
                deps="../lambda/input/mime-detect.c ../lambda/input/mime-types.c"
                ;;
            "test_mir.c")
                deps="../build/*.o -lmir -lc2mir"
                ;;
            "test_validator.c")
                deps="-I../include -I../lambda -I../lambda/validator"
                ;;
        esac
        
        print_status "Compiling $test_source with AddressSanitizer..."
        if clang $asan_flags -o "$test_binary" "$test_source" $deps $criterion_flags 2>/dev/null; then
            print_success "Compiled $test_binary successfully"
        else
            print_error "Failed to compile $test_source with AddressSanitizer"
        fi
    done
}

# Run tests with AddressSanitizer
run_asan_tests() {
    print_status "🧪 Running tests with AddressSanitizer..."
    
    local asan_binaries=(test_asan_*.exe)
    local total_tests=0
    local passed_tests=0
    
    for binary in "${asan_binaries[@]}"; do
        if [ -f "$binary" ]; then
            print_status "Running $binary..."
            
            # Set AddressSanitizer options
            export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=1"
            
            if ./"$binary" --verbose 2>/dev/null; then
                print_success "$binary passed memory leak check"
                passed_tests=$((passed_tests + 1))
            else
                print_error "$binary failed memory leak check"
            fi
            
            total_tests=$((total_tests + 1))
            
            # Cleanup
            rm -f "$binary"
        fi
    done
    
    echo ""
    print_status "📊 AddressSanitizer Results:"
    echo "   Total Tests: $total_tests"
    echo "   Passed: $passed_tests"
    echo "   Failed: $((total_tests - passed_tests))"
}

# Run tests with Valgrind
run_valgrind_tests() {
    print_status "🔍 Running tests with Valgrind..."
    
    # Compile without optimization for better stack traces
    local test_sources=(
        "test_strbuf.c"
        "test_variable_pool.c"
    )
    
    for test_source in "${test_sources[@]}"; do
        local test_binary="test_valgrind_${test_source%%.c}.exe"
        local deps=""
        
        case "$test_source" in
            "test_strbuf.c")
                deps="../lib/strbuf.c ../lib/mem-pool/src/variable.c ../lib/mem-pool/src/buffer.c ../lib/mem-pool/src/utils.c -I../lib/mem-pool/include"
                ;;
            "test_variable_pool.c")
                deps="../lib/mem-pool/src/variable.c ../lib/mem-pool/src/buffer.c ../lib/mem-pool/src/utils.c -I../lib/mem-pool/include"
                ;;
        esac
        
        print_status "Compiling $test_source for Valgrind..."
        if gcc -g -O0 -o "$test_binary" "$test_source" $deps -lcriterion 2>/dev/null; then
            print_success "Compiled $test_binary successfully"
            
            print_status "Running Valgrind on $test_binary..."
            valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./"$test_binary" --verbose >/dev/null 2>valgrind_$test_source.log
            
            if [ $? -eq 0 ]; then
                print_success "$test_binary passed Valgrind check"
            else
                print_error "$test_binary failed Valgrind check - see valgrind_$test_source.log"
            fi
            
            rm -f "$test_binary"
        else
            print_error "Failed to compile $test_source for Valgrind"
        fi
    done
}

# Main execution
cd test

print_status "🚀 Starting memory leak detection..."
echo ""

# Run AddressSanitizer tests
compile_with_asan
run_asan_tests

# Run Valgrind tests if available
echo ""
if check_valgrind; then
    run_valgrind_tests
fi

print_success "🎉 Memory leak detection completed!"
echo ""
print_status "💡 Review any generated log files for detailed memory analysis"
