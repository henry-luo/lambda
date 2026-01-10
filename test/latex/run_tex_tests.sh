#!/bin/bash
# run_tex_tests.sh - Run TeX typesetting comparison tests
#
# Usage:
#   ./test/latex/run_tex_tests.sh           # Run all tests
#   ./test/latex/run_tex_tests.sh test_name # Run specific test

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_DIR="$SCRIPT_DIR"
REF_DIR="$TEST_DIR/reference"
OUT_DIR="$TEST_DIR/output"
COMPARE_SCRIPT="$PROJECT_ROOT/utils/compare_dvi_output.py"
LAMBDA_EXE="$PROJECT_ROOT/lambda.exe"

# tolerance in points
TOLERANCE=1.0

# create directories
mkdir -p "$REF_DIR" "$OUT_DIR"

# colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# counters
PASSED=0
FAILED=0
SKIPPED=0

# helper functions
log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((PASSED++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((FAILED++))
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1"
    ((SKIPPED++))
}

# generate reference DVI from .tex file
generate_reference() {
    local tex_file="$1"
    local base_name=$(basename "$tex_file" .tex)
    local dvi_file="$REF_DIR/$base_name.dvi"

    if [[ ! -f "$dvi_file" ]] || [[ "$tex_file" -nt "$dvi_file" ]]; then
        log_info "Generating reference DVI for $base_name..."
        (cd "$TEST_DIR" && latex -output-directory="$REF_DIR" -interaction=nonstopmode "$tex_file" > /dev/null 2>&1) || {
            log_skip "$base_name (latex failed)"
            return 1
        }
    fi
    return 0
}

# run lambda typesetting and compare
run_test() {
    local tex_file="$1"
    local base_name=$(basename "$tex_file" .tex)
    local dvi_file="$REF_DIR/$base_name.dvi"
    local json_file="$OUT_DIR/$base_name.json"

    # generate reference if needed
    if ! generate_reference "$tex_file"; then
        return
    fi

    # skip if reference doesn't exist
    if [[ ! -f "$dvi_file" ]]; then
        log_skip "$base_name (no reference DVI)"
        return
    fi

    # run Lambda typesetting (placeholder - actual command TBD)
    # For now, create a placeholder JSON
    if [[ ! -f "$LAMBDA_EXE" ]]; then
        log_skip "$base_name (lambda.exe not found)"
        return
    fi

    # TODO: Actual Lambda typesetting command
    # $LAMBDA_EXE typeset "$tex_file" -o "$json_file"

    # For now, skip since typeset command not implemented yet
    log_skip "$base_name (typeset not implemented)"
    return

    # compare
    if python3 "$COMPARE_SCRIPT" "$json_file" "$dvi_file" --tolerance "$TOLERANCE" > /dev/null 2>&1; then
        log_pass "$base_name"
    else
        log_fail "$base_name"
        if [[ "$VERBOSE" == "1" ]]; then
            python3 "$COMPARE_SCRIPT" "$json_file" "$dvi_file" --tolerance "$TOLERANCE" --verbose
        fi
    fi
}

# main
main() {
    echo "========================================"
    echo "TeX Typesetting Comparison Tests"
    echo "========================================"
    echo ""

    # check dependencies
    if ! command -v latex &> /dev/null; then
        echo "Error: latex not found. Install TeX distribution."
        exit 1
    fi

    if ! command -v python3 &> /dev/null; then
        echo "Error: python3 not found."
        exit 1
    fi

    # run tests
    if [[ -n "$1" ]]; then
        # run specific test
        tex_file="$TEST_DIR/$1.tex"
        if [[ -f "$tex_file" ]]; then
            run_test "$tex_file"
        else
            echo "Test not found: $1"
            exit 1
        fi
    else
        # run all tests
        for tex_file in "$TEST_DIR"/test_*.tex; do
            if [[ -f "$tex_file" ]]; then
                run_test "$tex_file"
            fi
        done
    fi

    # summary
    echo ""
    echo "========================================"
    echo "Summary: ${GREEN}$PASSED passed${NC}, ${RED}$FAILED failed${NC}, ${YELLOW}$SKIPPED skipped${NC}"
    echo "========================================"

    if [[ $FAILED -gt 0 ]]; then
        exit 1
    fi
}

main "$@"
