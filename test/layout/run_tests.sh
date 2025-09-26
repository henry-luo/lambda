#!/bin/bash

# =============================================================================
# Radiant Layout Engine Automated Testing Workflow
# =============================================================================
# 
# This script orchestrates the complete testing pipeline:
# 1. Generate HTML/CSS test cases
# 2. Extract browser reference data with Puppeteer
# 3. Compile and run C++ validation tests
# 4. Generate comprehensive reports
#
# Usage:
#   ./run_tests.sh [options]
#
# Options:
#   --generate-only    Only generate test cases, don't run extraction/validation
#   --extract-only     Only run browser extraction, skip test generation
#   --validate-only    Only run C++ validation, skip generation/extraction
#   --category <name>  Run tests for specific category (basic|intermediate|advanced)
#   --verbose          Enable verbose output
#   --clean            Clean previous results before running
#   --help             Show this help message
#
# =============================================================================

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TEST_DIR/../.." && pwd)"

# Default settings
GENERATE_TESTS=true
EXTRACT_REFERENCES=true
RUN_VALIDATION=true
CATEGORY=""
VERBOSE=false
CLEAN=false
PARALLEL_JOBS=4

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# =============================================================================
# Utility Functions
# =============================================================================

log() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[$(date +'%H:%M:%S')] ✅ $1${NC}"
}

log_warning() {
    echo -e "${YELLOW}[$(date +'%H:%M:%S')] ⚠️  $1${NC}"
}

log_error() {
    echo -e "${RED}[$(date +'%H:%M:%S')] ❌ $1${NC}"
}

log_step() {
    echo -e "\n${PURPLE}=== $1 ===${NC}"
}

check_dependencies() {
    log "Checking dependencies..."
    
    local missing_deps=()
    
    # Check Node.js and npm
    if ! command -v node &> /dev/null; then
        missing_deps+=("node.js")
    fi
    
    if ! command -v npm &> /dev/null; then
        missing_deps+=("npm")
    fi
    
    # Check C++ compiler
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        missing_deps+=("g++ or clang++")
    fi
    
    # Check make
    if ! command -v make &> /dev/null; then
        missing_deps+=("make")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        echo "Please install the missing dependencies and try again."
        exit 1
    fi
    
    log_success "All dependencies available"
}

setup_environment() {
    log "Setting up test environment..."
    
    cd "$TEST_DIR"
    
    # Create necessary directories
    mkdir -p data/{basic,intermediate,advanced}
    mkdir -p reference/{basic,intermediate,advanced}
    mkdir -p reports
    mkdir -p tools/node_modules
    
    # Install Node.js dependencies
    if [ ! -d "tools/node_modules/puppeteer" ]; then
        log "Installing Node.js dependencies..."
        cd tools
        
        # Create package.json if it doesn't exist
        if [ ! -f package.json ]; then
            cat > package.json << EOF
{
  "name": "radiant-layout-tests",
  "version": "1.0.0",
  "description": "Automated layout testing for Radiant engine",
  "main": "extract_layout.js",
  "scripts": {
    "extract": "node extract_layout.js",
    "generate": "node generate_tests.js"
  },
  "dependencies": {
    "puppeteer": "^21.0.0"
  },
  "devDependencies": {},
  "author": "Radiant Layout Team",
  "license": "MIT"
}
EOF
        fi
        
        npm install
        cd ..
    fi
    
    log_success "Environment setup complete"
}

# =============================================================================
# Test Generation
# =============================================================================

generate_test_cases() {
    log_step "STEP 1: Generating Test Cases"
    
    if [ "$GENERATE_TESTS" != true ]; then
        log "Skipping test generation (--extract-only or --validate-only specified)"
        return 0
    fi
    
    cd "$TEST_DIR/tools"
    
    if [ -n "$CATEGORY" ]; then
        log "Generating test cases for category: $CATEGORY"
        node generate_tests.js category "$CATEGORY"
    else
        log "Generating test cases for all categories..."
        node generate_tests.js all
    fi
    
    # Count generated tests
    local basic_count=$(find ../data/basic -name "*.html" 2>/dev/null | wc -l)
    local intermediate_count=$(find ../data/intermediate -name "*.html" 2>/dev/null | wc -l)
    local advanced_count=$(find ../data/advanced -name "*.html" 2>/dev/null | wc -l)
    local total_count=$((basic_count + intermediate_count + advanced_count))
    
    log_success "Generated $total_count test cases"
    log "  Basic: $basic_count, Intermediate: $intermediate_count, Advanced: $advanced_count"
}

# =============================================================================
# Browser Reference Extraction
# =============================================================================

extract_browser_references() {
    log_step "STEP 2: Extracting Browser References"
    
    if [ "$EXTRACT_REFERENCES" != true ]; then
        log "Skipping reference extraction (--validate-only specified)"
        return 0
    fi
    
    cd "$TEST_DIR/tools"
    
    # Check if we have test cases to process
    local test_count=0
    if [ -n "$CATEGORY" ]; then
        test_count=$(find "../data/$CATEGORY" -name "*.html" 2>/dev/null | wc -l)
        if [ $test_count -eq 0 ]; then
            log_warning "No test cases found in category: $CATEGORY"
            return 0
        fi
        
        log "Extracting browser references for category: $CATEGORY"
        node extract_layout.js category "$CATEGORY"
    else
        test_count=$(find ../data -name "*.html" 2>/dev/null | wc -l)
        if [ $test_count -eq 0 ]; then
            log_warning "No test cases found to process"
            return 0
        fi
        
        log "Extracting browser references for all categories..."
        log "This may take a while as we launch browser instances..."
        
        # Run extraction with progress indication
        node extract_layout.js all
    fi
    
    # Verify extraction results
    local ref_count=0
    if [ -n "$CATEGORY" ]; then
        ref_count=$(find "../reference/$CATEGORY" -name "*.json" 2>/dev/null | wc -l)
    else
        ref_count=$(find ../reference -name "*.json" 2>/dev/null | wc -l)
    fi
    
    if [ $ref_count -eq 0 ]; then
        log_error "No reference files were generated"
        return 1
    fi
    
    log_success "Extracted $ref_count browser reference files"
    
    # Generate extraction summary if available
    if [ -f "../reports/extraction_summary.json" ]; then
        log "Extraction summary saved to reports/extraction_summary.json"
    fi
}

# =============================================================================
# C++ Test Compilation and Execution
# =============================================================================

compile_validation_tests() {
    log_step "STEP 3: Compiling Validation Tests"
    
    cd "$TEST_DIR/tools"
    
    # Create Makefile if it doesn't exist
    if [ ! -f Makefile ]; then
        log "Creating Makefile for test compilation..."
        cat > Makefile << 'EOF'
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I.
LDFLAGS = -ljson-c -lgtest -lgtest_main -pthread

# Source files
FRAMEWORK_SOURCES = layout_test_framework.cpp
TEST_SOURCES = test_layout_auto.cpp
OBJECTS = $(FRAMEWORK_SOURCES:.cpp=.o) $(TEST_SOURCES:.cpp=.o)

# Target
TARGET = test_layout_auto

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

test: $(TARGET)
	./$(TARGET) --gtest_output=xml:../reports/test_results.xml

.PHONY: all clean test
EOF
    fi
    
    # Check for required system libraries
    log "Checking for required libraries..."
    
    local missing_libs=()
    
    # Check for json-c
    if ! pkg-config --exists json-c; then
        missing_libs+=("json-c")
    fi
    
    # Check for gtest
    if ! pkg-config --exists gtest; then
        if [ ! -f /usr/lib/libgtest.a ] && [ ! -f /usr/local/lib/libgtest.a ]; then
            missing_libs+=("gtest")
        fi
    fi
    
    if [ ${#missing_libs[@]} -ne 0 ]; then
        log_warning "Missing libraries: ${missing_libs[*]}"
        log "Attempting to install or provide alternative..."
        
        # Try to provide fallback compilation options
        if [[ " ${missing_libs[*]} " =~ " json-c " ]]; then
            log_warning "json-c not found. Please install: sudo apt-get install libjson-c-dev (Ubuntu) or brew install json-c (macOS)"
        fi
        
        if [[ " ${missing_libs[*]} " =~ " gtest " ]]; then
            log_warning "Google Test not found. Please install: sudo apt-get install libgtest-dev (Ubuntu) or brew install googletest (macOS)"
        fi
        
        log_error "Cannot compile without required libraries"
        return 1
    fi
    
    # Compile
    log "Compiling validation framework and tests..."
    make clean 2>/dev/null || true
    
    if make all; then
        log_success "Compilation successful"
    else
        log_error "Compilation failed"
        return 1
    fi
}

run_validation_tests() {
    log_step "STEP 4: Running Validation Tests"
    
    if [ "$RUN_VALIDATION" != true ]; then
        log "Skipping validation tests (--generate-only or --extract-only specified)"
        return 0
    fi
    
    cd "$TEST_DIR/tools"
    
    # Check if executable exists
    if [ ! -f test_layout_auto ]; then
        log_error "Test executable not found. Compilation may have failed."
        return 1
    fi
    
    # Check if we have reference data
    local ref_count=0
    if [ -n "$CATEGORY" ]; then
        ref_count=$(find "../reference/$CATEGORY" -name "*.json" 2>/dev/null | wc -l)
    else
        ref_count=$(find ../reference -name "*.json" 2>/dev/null | wc -l)
    fi
    
    if [ $ref_count -eq 0 ]; then
        log_error "No reference data found. Please run extraction step first."
        return 1
    fi
    
    log "Running validation tests against $ref_count reference files..."
    
    # Set up test environment variables
    export GTEST_COLOR=yes
    export GTEST_PRINT_TIME=1
    
    # Run tests with appropriate verbosity
    local test_args="--gtest_output=xml:../reports/test_results.xml"
    
    if [ "$VERBOSE" = true ]; then
        test_args="$test_args --gtest_print_time=1"
    fi
    
    # Change to parent directory so relative paths work
    cd "$TEST_DIR"
    
    if ./tools/test_layout_auto $test_args; then
        log_success "All validation tests completed"
    else
        local exit_code=$?
        log_warning "Some validation tests failed or had issues (exit code: $exit_code)"
        
        # Don't fail the entire pipeline for test failures - they might be expected
        # during development
        if [ $exit_code -gt 1 ]; then
            log_error "Test execution had serious errors"
            return 1
        fi
    fi
    
    # Check if XML results were generated
    if [ -f "reports/test_results.xml" ]; then
        log "Test results saved to reports/test_results.xml"
    fi
}

# =============================================================================
# Report Generation
# =============================================================================

generate_reports() {
    log_step "STEP 5: Generating Reports"
    
    cd "$TEST_DIR"
    
    local report_dir="reports"
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    
    # Create summary report
    log "Generating test summary report..."
    
    local summary_file="$report_dir/test_summary_$timestamp.txt"
    
    cat > "$summary_file" << EOF
Radiant Layout Engine Test Summary
==================================
Generated: $(date)
Test Directory: $TEST_DIR

EOF
    
    # Count test files
    local basic_tests=$(find data/basic -name "*.html" 2>/dev/null | wc -l)
    local intermediate_tests=$(find data/intermediate -name "*.html" 2>/dev/null | wc -l)
    local advanced_tests=$(find data/advanced -name "*.html" 2>/dev/null | wc -l)
    local total_tests=$((basic_tests + intermediate_tests + advanced_tests))
    
    cat >> "$summary_file" << EOF
Test Cases Generated:
  Basic: $basic_tests
  Intermediate: $intermediate_tests
  Advanced: $advanced_tests
  Total: $total_tests

EOF
    
    # Count reference files
    local basic_refs=$(find reference/basic -name "*.json" 2>/dev/null | wc -l)
    local intermediate_refs=$(find reference/intermediate -name "*.json" 2>/dev/null | wc -l)
    local advanced_refs=$(find reference/advanced -name "*.json" 2>/dev/null | wc -l)
    local total_refs=$((basic_refs + intermediate_refs + advanced_refs))
    
    cat >> "$summary_file" << EOF
Browser References Extracted:
  Basic: $basic_refs
  Intermediate: $intermediate_refs
  Advanced: $advanced_refs
  Total: $total_refs

EOF
    
    # Parse GTest XML results if available
    if [ -f "$report_dir/test_results.xml" ]; then
        log "Parsing test execution results..."
        
        # Extract basic stats from XML (simple grep-based parsing)
        local total_gtest=$(grep -o 'tests="[0-9]*"' "$report_dir/test_results.xml" | head -1 | grep -o '[0-9]*')
        local failures_gtest=$(grep -o 'failures="[0-9]*"' "$report_dir/test_results.xml" | head -1 | grep -o '[0-9]*')
        local errors_gtest=$(grep -o 'errors="[0-9]*"' "$report_dir/test_results.xml" | head -1 | grep -o '[0-9]*')
        
        if [ -n "$total_gtest" ]; then
            local passed_gtest=$((total_gtest - failures_gtest - errors_gtest))
            local success_rate=$(echo "scale=1; $passed_gtest * 100 / $total_gtest" | bc -l 2>/dev/null || echo "N/A")
            
            cat >> "$summary_file" << EOF
Validation Test Results:
  Total Tests: $total_gtest
  Passed: $passed_gtest
  Failed: $failures_gtest
  Errors: $errors_gtest
  Success Rate: $success_rate%

EOF
        fi
    fi
    
    # Add file locations
    cat >> "$summary_file" << EOF
Generated Files:
  Test Data: data/
  Browser References: reference/
  Test Results: $report_dir/test_results.xml
  Extraction Summary: $report_dir/extraction_summary.json
  This Summary: $summary_file

EOF
    
    log_success "Summary report generated: $summary_file"
    
    # Create a symlink to latest summary
    ln -sf "$(basename "$summary_file")" "$report_dir/latest_summary.txt"
    
    # Generate HTML report if we have results
    if [ -f "$report_dir/test_results.xml" ] && command -v python3 &> /dev/null; then
        log "Generating HTML report..."
        
        # Simple HTML report generator
        python3 -c "
import xml.etree.ElementTree as ET
import sys
from datetime import datetime

try:
    tree = ET.parse('$report_dir/test_results.xml')
    root = tree.getroot()
    
    html = '''<!DOCTYPE html>
<html>
<head>
    <title>Radiant Layout Test Results</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .header { background: #f0f0f0; padding: 20px; border-radius: 5px; }
        .stats { display: flex; gap: 20px; margin: 20px 0; }
        .stat { background: #e8f4fd; padding: 15px; border-radius: 5px; text-align: center; }
        .passed { background: #d4edda; }
        .failed { background: #f8d7da; }
        .test-case { margin: 10px 0; padding: 10px; border-left: 4px solid #ccc; }
        .test-case.pass { border-left-color: #28a745; }
        .test-case.fail { border-left-color: #dc3545; }
    </style>
</head>
<body>
    <div class=\"header\">
        <h1>Radiant Layout Engine Test Results</h1>
        <p>Generated: ''' + datetime.now().strftime('%Y-%m-%d %H:%M:%S') + '''</p>
    </div>
'''
    
    total = int(root.get('tests', 0))
    failures = int(root.get('failures', 0))
    errors = int(root.get('errors', 0))
    passed = total - failures - errors
    
    html += f'''
    <div class=\"stats\">
        <div class=\"stat\">
            <h3>{total}</h3>
            <p>Total Tests</p>
        </div>
        <div class=\"stat passed\">
            <h3>{passed}</h3>
            <p>Passed</p>
        </div>
        <div class=\"stat failed\">
            <h3>{failures + errors}</h3>
            <p>Failed/Errors</p>
        </div>
    </div>
    
    <h2>Test Cases</h2>
'''
    
    for testcase in root.findall('.//testcase'):
        name = testcase.get('name', 'Unknown')
        classname = testcase.get('classname', '')
        time = testcase.get('time', '0')
        
        failure = testcase.find('failure')
        error = testcase.find('error')
        
        status = 'pass'
        message = ''
        
        if failure is not None:
            status = 'fail'
            message = failure.get('message', 'Test failed')
        elif error is not None:
            status = 'fail'
            message = error.get('message', 'Test error')
        
        html += f'''
        <div class=\"test-case {status}\">
            <h4>{classname}.{name}</h4>
            <p>Time: {time}s</p>
            {f'<p><strong>Issue:</strong> {message}</p>' if message else ''}
        </div>
        '''
    
    html += '''
</body>
</html>
'''
    
    with open('$report_dir/test_results.html', 'w') as f:
        f.write(html)
    
    print('HTML report generated successfully')
    
except Exception as e:
    print(f'Failed to generate HTML report: {e}')
    sys.exit(1)
" && log_success "HTML report generated: $report_dir/test_results.html"
    fi
    
    # Show summary to user
    echo
    log_step "TEST EXECUTION COMPLETE"
    echo
    cat "$summary_file"
}

# =============================================================================
# Cleanup Functions
# =============================================================================

clean_previous_results() {
    if [ "$CLEAN" = true ]; then
        log "Cleaning previous results..."
        
        cd "$TEST_DIR"
        
        # Remove generated test files
        rm -rf data/*/test_*.html
        
        # Remove reference files
        rm -rf reference/*/*.json
        
        # Remove reports (keep directory)
        rm -rf reports/*.xml reports/*.json reports/*.html reports/*.txt
        
        # Remove compiled binaries
        rm -f tools/test_layout_auto tools/*.o
        
        log_success "Previous results cleaned"
    fi
}

# =============================================================================
# Main Execution
# =============================================================================

show_help() {
    cat << EOF
Radiant Layout Engine Automated Testing Workflow

Usage: $0 [options]

Options:
  --generate-only     Only generate test cases, don't run extraction/validation
  --extract-only      Only run browser extraction, skip test generation
  --validate-only     Only run C++ validation, skip generation/extraction
  --category <name>   Run tests for specific category (basic|intermediate|advanced)
  --verbose           Enable verbose output
  --clean             Clean previous results before running
  --help              Show this help message

Examples:
  $0                                    # Run complete pipeline
  $0 --category basic                   # Run only basic tests
  $0 --generate-only --category basic   # Generate only basic test cases
  $0 --extract-only                     # Extract browser references for existing tests
  $0 --validate-only                    # Run validation tests only
  $0 --clean --verbose                  # Clean and run with verbose output

Environment:
  The script expects to be run from or find the test/layout directory.
  Required dependencies: node.js, npm, g++/clang++, make, json-c, gtest

Generated files:
  data/           - HTML/CSS test cases
  reference/      - Browser-extracted layout references (JSON)
  reports/        - Test results and summary reports
  tools/          - Compiled test executables

EOF
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --generate-only)
                GENERATE_TESTS=true
                EXTRACT_REFERENCES=false
                RUN_VALIDATION=false
                shift
                ;;
            --extract-only)
                GENERATE_TESTS=false
                EXTRACT_REFERENCES=true
                RUN_VALIDATION=false
                shift
                ;;
            --validate-only)
                GENERATE_TESTS=false
                EXTRACT_REFERENCES=false
                RUN_VALIDATION=true
                shift
                ;;
            --category)
                CATEGORY="$2"
                if [[ ! "$CATEGORY" =~ ^(basic|intermediate|advanced)$ ]]; then
                    log_error "Invalid category: $CATEGORY. Must be basic, intermediate, or advanced"
                    exit 1
                fi
                shift 2
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --clean)
                CLEAN=true
                shift
                ;;
            --help)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

main() {
    # Parse command line arguments
    parse_arguments "$@"
    
    # Print banner
    echo -e "${CYAN}"
    echo "=============================================="
    echo "  Radiant Layout Engine Testing Pipeline"
    echo "=============================================="
    echo -e "${NC}"
    echo
    
    # Environment checks
    check_dependencies
    setup_environment
    
    # Clean if requested
    clean_previous_results
    
    # Main execution steps
    local start_time=$(date +%s)
    
    generate_test_cases
    extract_browser_references
    compile_validation_tests
    run_validation_tests
    generate_reports
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    echo
    log_step "PIPELINE COMPLETE"
    log_success "Total execution time: ${duration}s"
    echo
    
    # Final status
    if [ -f "$TEST_DIR/reports/latest_summary.txt" ]; then
        echo -e "${CYAN}Final Summary:${NC}"
        cat "$TEST_DIR/reports/latest_summary.txt"
    fi
}

# Error handling
trap 'log_error "Script interrupted"; exit 1' INT TERM

# Run main function with all arguments
main "$@"
