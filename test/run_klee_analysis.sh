#!/bin/bash

# KLEE Analysis Script for Lambda Script
# This script runs KLEE symbolic execution on Lambda Script components
# to automatically discover runtime issues like division by zero,
# buffer overflows, null pointer dereferences, etc.

set -e  # Exit on any error

# Configuration
KLEE_BUILD_DIR="build_klee"
KLEE_OUTPUT_DIR="klee_output"
TIMEOUT="300"  # 5 minutes per test
MAX_MEMORY="1000"  # 1GB memory limit
MAX_INSTRUCTIONS="10000000"  # 10M instruction limit

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

warn() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Check if KLEE is installed
check_klee_installation() {
    log "Checking KLEE installation..."
    
    if ! command -v klee &> /dev/null; then
        error "KLEE is not installed or not in PATH"
        error "Please install KLEE from: https://klee.github.io/"
        exit 1
    fi
    
    if ! command -v klee-clang &> /dev/null; then
        error "klee-clang is not installed or not in PATH"
        exit 1
    fi
    
    success "KLEE installation verified"
}

# Setup build directories
setup_directories() {
    log "Setting up build directories..."
    
    mkdir -p "$KLEE_BUILD_DIR"
    mkdir -p "$KLEE_OUTPUT_DIR"
    
    # Create subdirectories for each test
    mkdir -p "$KLEE_OUTPUT_DIR/arithmetic"
    mkdir -p "$KLEE_OUTPUT_DIR/strings"
    mkdir -p "$KLEE_OUTPUT_DIR/arrays"
    mkdir -p "$KLEE_OUTPUT_DIR/memory_pool"
    
    success "Directories created"
}

# Compile test harness to LLVM bitcode
compile_test() {
    local test_name="$1"
    local test_file="klee/${test_name}.c"
    local output_bc="../${KLEE_BUILD_DIR}/${test_name}.bc"
    
    log "Compiling $test_name for KLEE analysis..."
    
    if [ ! -f "$test_file" ]; then
        error "Test file not found: $test_file"
        return 1
    fi
    
    # Compile with KLEE-specific flags
    klee-clang \
        -I.. \
        -I../lib \
        -I../lambda \
        -I../lib/mem-pool/include \
        -emit-llvm \
        -c \
        -g \
        -O0 \
        -Xclang -disable-O0-optnone \
        -DKLEE_ANALYSIS \
        "$test_file" \
        -o "$output_bc"
    
    if [ $? -eq 0 ]; then
        success "Compiled $test_name successfully"
        return 0
    else
        error "Failed to compile $test_name"
        return 1
    fi
}

# Run KLEE on a test harness
run_klee_test() {
    local test_name="$1"
    local bitcode_file="../${KLEE_BUILD_DIR}/${test_name}.bc"
    local output_dir="../${KLEE_OUTPUT_DIR}/${test_name}"
    
    log "Running KLEE analysis on $test_name..."
    
    if [ ! -f "$bitcode_file" ]; then
        error "Bitcode file not found: $bitcode_file"
        return 1
    fi
    
    # Run KLEE with comprehensive options
    klee \
        --output-dir="$output_dir" \
        --max-time="$TIMEOUT" \
        --max-memory="$MAX_MEMORY" \
        --max-instructions="$MAX_INSTRUCTIONS" \
        --write-test-cases \
        --write-paths \
        --write-sym-paths \
        --write-cov \
        --optimize \
        --use-forked-solver \
        --use-cex-cache \
        --libc=uclibc \
        --posix-runtime \
        --search=dfs \
        --use-batching-search \
        --batch-instructions=10000 \
        "$bitcode_file" 2>&1 | tee "${output_dir}/klee.log"
    
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        success "KLEE analysis completed for $test_name"
    else
        warn "KLEE analysis finished with issues for $test_name (exit code: $exit_code)"
    fi
    
    return $exit_code
}

# Analyze KLEE results
analyze_results() {
    local test_name="$1"
    local output_dir="../${KLEE_OUTPUT_DIR}/${test_name}"
    
    log "Analyzing results for $test_name..."
    
    if [ ! -d "$output_dir" ]; then
        error "Output directory not found: $output_dir"
        return 1
    fi
    
    # Count test cases generated
    local test_count=$(find "$output_dir" -name "test*.ktest" | wc -l)
    log "Generated $test_count test cases for $test_name"
    
    # Check for assertion failures (errors)
    local error_files=$(find "$output_dir" -name "test*.err")
    local error_count=$(echo "$error_files" | grep -c "test.*\.err" || true)
    
    if [ $error_count -gt 0 ]; then
        error "Found $error_count errors in $test_name:"
        echo "$error_files" | while read -r err_file; do
            if [ -f "$err_file" ]; then
                echo "  Error in $(basename "$err_file"):"
                cat "$err_file" | sed 's/^/    /'
                echo
            fi
        done
    else
        success "No errors found in $test_name"
    fi
    
    # Check coverage information
    if [ -f "$output_dir/run.stats" ]; then
        log "Coverage statistics for $test_name:"
        grep -E "(Instructions|Branches|TotalInstructions|CompletedPaths)" "$output_dir/run.stats" | sed 's/^/  /'
    fi
    
    return $error_count
}

# Generate detailed report
generate_report() {
    local report_file="../klee_analysis_report.md"
    
    log "Generating comprehensive analysis report..."
    
    cat > "$report_file" << EOF
# KLEE Analysis Report for Lambda Script

**Generated on:** $(date)  
**Analysis Duration:** $(date -d@$SECONDS -u +%H:%M:%S)

## Executive Summary

This report contains the results of symbolic execution analysis using KLEE 
to identify potential runtime issues in the Lambda Script codebase.

## Test Results

EOF
    
    local total_errors=0
    local total_tests=0
    
    # Analyze each test output
    for test_dir in "../${KLEE_OUTPUT_DIR}"/*/; do
        if [ -d "$test_dir" ]; then
            test_name=$(basename "$test_dir")
            echo "### Test: $test_name" >> "$report_file"
            echo "" >> "$report_file"
            
            # Count test cases
            test_count=$(find "$test_dir" -name "test*.ktest" | wc -l)
            echo "- **Test cases generated:** $test_count" >> "$report_file"
            total_tests=$((total_tests + test_count))
            
            # Check for errors
            error_files=$(find "$test_dir" -name "test*.err")
            error_count=$(echo "$error_files" | grep -c "test.*\.err" || true)
            
            if [ $error_count -gt 0 ]; then
                echo "- **🚨 ERRORS FOUND:** $error_count" >> "$report_file"
                total_errors=$((total_errors + error_count))
                echo "" >> "$report_file"
                echo "#### Error Details:" >> "$report_file"
                echo "" >> "$report_file"
                
                echo "$error_files" | while read -r err_file; do
                    if [ -f "$err_file" ]; then
                        test_case=$(basename "$err_file" .err)
                        echo "**Error in $test_case:**" >> "$report_file"
                        echo "\`\`\`" >> "$report_file"
                        cat "$err_file" >> "$report_file"
                        echo "\`\`\`" >> "$report_file"
                        echo "" >> "$report_file"
                        
                        # Try to find corresponding test case
                        ktest_file="${test_dir}/${test_case}.ktest"
                        if [ -f "$ktest_file" ]; then
                            echo "**Test case inputs:**" >> "$report_file"
                            echo "\`\`\`" >> "$report_file"
                            ktest-tool "$ktest_file" 2>/dev/null >> "$report_file" || echo "Unable to decode test case" >> "$report_file"
                            echo "\`\`\`" >> "$report_file"
                            echo "" >> "$report_file"
                        fi
                    fi
                done
            else
                echo "- **✅ No errors found**" >> "$report_file"
            fi
            
            # Coverage information
            if [ -f "$test_dir/run.stats" ]; then
                echo "- **Coverage Statistics:**" >> "$report_file"
                echo "  \`\`\`" >> "$report_file"
                grep -E "(Instructions|Branches|TotalInstructions|CompletedPaths)" "$test_dir/run.stats" >> "$report_file"
                echo "  \`\`\`" >> "$report_file"
            fi
            
            echo "" >> "$report_file"
        fi
    done
    
    # Add summary
    cat >> "$report_file" << EOF

## Summary

- **Total test cases generated:** $total_tests
- **Total errors found:** $total_errors
- **Analysis status:** $([ $total_errors -eq 0 ] && echo "✅ PASSED" || echo "❌ FAILED")

## Recommendations

EOF
    
    if [ $total_errors -gt 0 ]; then
        cat >> "$report_file" << EOF
### Critical Issues Found

The analysis discovered $total_errors potential runtime issues that should be addressed:

1. **Review error details** above for specific problematic inputs
2. **Add input validation** to prevent invalid conditions
3. **Implement bounds checking** for array and buffer operations
4. **Add null pointer checks** where indicated
5. **Consider overflow protection** for arithmetic operations

### Next Steps

1. Fix the identified issues in the source code
2. Add regression tests based on the generated test cases
3. Re-run KLEE analysis to verify fixes
4. Consider integrating KLEE into CI/CD pipeline

EOF
    else
        cat >> "$report_file" << EOF
### No Critical Issues Found

The analysis did not discover any assertion failures, indicating that:

1. **Input validation** appears to be working correctly
2. **Bounds checking** is properly implemented
3. **Null pointer handling** is adequate
4. **Arithmetic operations** have appropriate safeguards

This is a positive result, but consider:

1. **Expanding test coverage** with additional harnesses
2. **Testing with larger input spaces** (longer timeouts)
3. **Adding more complex test scenarios**

EOF
    fi
    
    success "Report generated: $report_file"
    return $total_errors
}

# Main execution
main() {
    log "Starting KLEE analysis for Lambda Script"
    
    # Check prerequisites
    check_klee_installation
    
    # Setup environment
    setup_directories
    
    # List of test harnesses to run
    local tests=("test_arithmetic" "test_strings" "test_arrays" "test_memory_pool" "test_validation")
    local failed_tests=0
    local total_errors=0
    
    # Compile and run each test
    for test in "${tests[@]}"; do
        log "Processing test: $test"
        
        # Compile test
        if ! compile_test "$test"; then
            error "Failed to compile $test, skipping..."
            failed_tests=$((failed_tests + 1))
            continue
        fi
        
        # Run KLEE
        if ! run_klee_test "$test"; then
            warn "KLEE analysis had issues for $test, continuing..."
        fi
        
        # Analyze results
        analyze_results "$test"
        local test_errors=$?
        total_errors=$((total_errors + test_errors))
    done
    
    # Generate comprehensive report
    generate_report
    local report_errors=$?
    
    # Final summary
    echo
    log "KLEE Analysis Complete"
    echo "===================="
    echo "Tests processed: ${#tests[@]}"
    echo "Compilation failures: $failed_tests"
    echo "Total errors found: $total_errors"
    echo "Report: klee_analysis_report.md"
    echo
    
    if [ $total_errors -eq 0 ] && [ $failed_tests -eq 0 ]; then
        success "All tests passed! No critical issues found."
        exit 0
    elif [ $failed_tests -gt 0 ]; then
        error "Some tests failed to compile or run."
        exit 2
    else
        warn "Analysis complete but $total_errors issues found. Review report for details."
        exit 1
    fi
}

# Run main function
main "$@"
