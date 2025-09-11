#!/bin/bash

# Docker-based KLEE Analysis Runner for Lambda Script
# This script runs KLEE analysis using Docker containers

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KLEE_BUILD_DIR="$PROJECT_ROOT/build_klee"
KLEE_OUTPUT_DIR="$PROJECT_ROOT/klee_output"
KLEE_TEST_DIR="$PROJECT_ROOT/test/klee"

# KLEE test files
KLEE_TESTS=(
    "test_arithmetic_simple"
    "test_strings_simple" 
    "test_arrays_simple"
)

# KLEE compilation flags
KLEE_CFLAGS="-emit-llvm -c -g -O0 -DKLEE_ANALYSIS"

# KLEE execution flags
KLEE_FLAGS="--max-time=60"

# Check if Docker is available
check_docker() {
    if ! command -v docker &> /dev/null; then
        error "Docker is not installed. Please run: make klee-install-docker"
        exit 1
    fi
    
    if ! docker info >/dev/null 2>&1; then
        error "Docker is not running. Please start Docker Desktop."
        exit 1
    fi
    
    # Check if KLEE wrapper scripts exist
    if ! command -v klee &> /dev/null; then
        error "KLEE Docker wrappers not found. Please run: make klee-install-docker"
        exit 1
    fi
}

# Setup directories
setup_directories() {
    log "Setting up KLEE directories..."
    mkdir -p "$KLEE_BUILD_DIR"
    mkdir -p "$KLEE_OUTPUT_DIR"
    
    # Clean previous results
    rm -rf "$KLEE_BUILD_DIR"/*.bc "$KLEE_OUTPUT_DIR"/*
    
    success "Directories prepared"
}

# Compile KLEE test cases
compile_tests() {
    log "Compiling KLEE test cases..."
    
    cd "$PROJECT_ROOT"
    local success_count=0
    local total_count=${#KLEE_TESTS[@]}
    
    for test_name in "${KLEE_TESTS[@]}"; do
        local source_file="test/klee/${test_name}.c"
        local bitcode_file="build_klee/${test_name}.bc"
        
        if [ ! -f "$source_file" ]; then
            warn "Source file not found: $source_file"
            continue
        fi
        
        log "Compiling $test_name..."
        
        # Use Docker-based klee-clang with relative paths
        if klee-clang $KLEE_CFLAGS "$source_file" -o "$bitcode_file" 2>/dev/null; then
            success "✓ Compiled $test_name"
            ((success_count++))
        else
            error "✗ Failed to compile $test_name"
            # Show compilation errors for debugging
            echo "Compilation command:"
            echo "klee-clang $KLEE_CFLAGS $source_file -o $bitcode_file"
        fi
    done
    
    if [ $success_count -eq 0 ]; then
        error "No test cases compiled successfully"
        exit 1
    fi
    
    success "Compiled $success_count/$total_count test cases"
}

# Run KLEE symbolic execution
run_symbolic_execution() {
    log "Running KLEE symbolic execution..."
    
    cd "$PROJECT_ROOT"
    local success_count=0
    local total_tests=0
    
    for test_name in "${KLEE_TESTS[@]}"; do
        local bitcode_file="build_klee/${test_name}.bc"
        local output_dir="klee_output/${test_name}"
        
        if [ ! -f "$bitcode_file" ]; then
            warn "Skipping $test_name (bitcode not found)"
            continue
        fi
        
        log "Running symbolic execution for $test_name..."
        
        # Remove output directory if it exists (KLEE requires it not to exist)
        rm -rf "$output_dir"
        
        # Run KLEE with Docker using relative paths
        if klee --output-dir="$output_dir" $KLEE_FLAGS "$bitcode_file"; then
            # Count generated test cases
            local test_count=$(find "$output_dir" -name "test*.ktest" | wc -l)
            success "✓ $test_name: Generated $test_count test cases"
            ((success_count++))
            ((total_tests += test_count))
        else
            error "✗ Failed to run $test_name"
            # Show KLEE output for debugging
            echo "KLEE command:"
            echo "klee --output-dir=$output_dir $KLEE_FLAGS $bitcode_file"
        fi
    done
    
    if [ $success_count -eq 0 ]; then
        error "No symbolic execution runs completed successfully"
        exit 1
    fi
    
    success "Completed $success_count symbolic execution runs"
    success "Generated $total_tests total test cases"
}

# Analyze results
analyze_results() {
    log "Analyzing KLEE results..."
    
    cd "$PROJECT_ROOT"
    local total_paths=0
    local total_errors=0
    local total_warnings=0
    
    echo "📊 KLEE Analysis Summary"
    echo "========================"
    
    for test_name in "${KLEE_TESTS[@]}"; do
        local output_dir="klee_output/${test_name}"
        
        if [ ! -d "$output_dir" ]; then
            continue
        fi
        
        # Count test cases and paths
        local test_cases=$(find "$output_dir" -name "test*.ktest" | wc -l)
        local error_cases=$(find "$output_dir" -name "test*.ktest.err" 2>/dev/null | wc -l)
        
        echo
        echo "🧪 $test_name:"
        echo "   Test cases: $test_cases"
        
        if [ $error_cases -gt 0 ]; then
            echo "   Errors found: $error_cases"
            ((total_errors += error_cases))
        fi
        
        # Show some sample test cases
        if [ $test_cases -gt 0 ]; then
            echo "   Sample test case:"
            local first_ktest=$(find "$output_dir" -name "test*.ktest" | head -1)
            if [ -n "$first_ktest" ]; then
                ktest-tool "$first_ktest" 2>/dev/null | head -10 | sed 's/^/     /'
            fi
        fi
        
        ((total_paths += test_cases))
    done
    
    echo
    echo "🎯 Overall Results:"
    echo "   Total execution paths: $total_paths"
    echo "   Total errors found: $total_errors"
    
    if [ $total_errors -gt 0 ]; then
        warn "KLEE found $total_errors potential issues!"
        echo "   Check individual test case files in klee_output for details"
    else
        success "No errors found in symbolic execution!"
    fi
}

# Show help
show_help() {
    echo "🔍 KLEE Docker Analysis Runner for Lambda Script"
    echo
    echo "Usage: $0 [COMMAND]"
    echo
    echo "Commands:"
    echo "  compile    - Compile test cases to LLVM bitcode"
    echo "  run        - Run symbolic execution on compiled tests"
    echo "  analyze    - Analyze results and generate summary"
    echo "  all        - Run complete analysis pipeline (default)"
    echo "  clean      - Clean generated files"
    echo "  help       - Show this help message"
    echo
    echo "Requirements:"
    echo "  - Docker Desktop installed and running"
    echo "  - KLEE Docker setup completed (make klee-install-docker)"
    echo
    echo "Examples:"
    echo "  $0              # Run complete analysis"
    echo "  $0 compile      # Just compile test cases"
    echo "  $0 run          # Just run symbolic execution"
    echo "  $0 analyze      # Just analyze results"
}

# Clean generated files
clean() {
    log "Cleaning KLEE generated files..."
    rm -rf "build_klee" "klee_output"
    success "Cleaned KLEE files"
}

# Main execution
main() {
    local command="${1:-all}"
    
    case "$command" in
        "help"|"-h"|"--help")
            show_help
            exit 0
            ;;
        "clean")
            clean
            exit 0
            ;;
        "check")
            check_docker
            success "Docker and KLEE setup verified"
            exit 0
            ;;
        "compile")
            check_docker
            setup_directories
            compile_tests
            ;;
        "run")
            check_docker
            run_symbolic_execution
            ;;
        "analyze")
            analyze_results
            ;;
        "all")
            log "🚀 Starting complete KLEE analysis pipeline..."
            check_docker
            setup_directories
            compile_tests
            run_symbolic_execution
            analyze_results
            success "🎉 KLEE analysis completed!"
            ;;
        *)
            error "Unknown command: $command"
            show_help
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
