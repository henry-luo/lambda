#!/bin/bash
# generate_latexjs_refs.sh - Generate latex.js HTML references for test fixtures
#
# This script processes .tex files under test/latexml/fixtures/latexjs and generates 
# HTML references using latex.js.
# Output: *.latexjs.html files alongside *.latexml.html files
#
# NOTE: Only processes fixtures under the latexjs/ subdirectory to avoid conflicts
# with LaTeXML-only fixtures that may use commands not supported by latex.js.
#
# Usage:
#   ./utils/generate_latexjs_refs.sh [--clean] [--verbose] [--test=<pattern>]
#
# Requires latex.js to be built: cd latex-js && npm install --legacy-peer-deps && npm run build

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

LATEXJS="$PROJECT_ROOT/latex-js/bin/latex.js"
# Only process latexjs fixtures - other fixtures use LaTeXML references
FIXTURES_DIR="$PROJECT_ROOT/test/latexml/fixtures/latexjs"
OUTPUT_DIR="$PROJECT_ROOT/test/latexml/expected/latexjs"

VERBOSE=false
CLEAN=false
TEST_PATTERN=""

# Parse arguments
for arg in "$@"; do
    case $arg in
        --verbose|-v)
            VERBOSE=true
            ;;
        --clean)
            CLEAN=true
            ;;
        --test=*)
            TEST_PATTERN="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [--clean] [--verbose] [--test=<pattern>]"
            echo "  --clean         Remove existing .latexjs.html files first"
            echo "  --verbose       Show detailed output"
            echo "  --test=<pat>    Only process files matching pattern"
            exit 0
            ;;
    esac
done

# Check for latex.js
if [ ! -f "$LATEXJS" ]; then
    echo "Error: latex.js not found at $LATEXJS"
    echo "Build it with: cd latex-js && npm install --legacy-peer-deps && npm run build"
    exit 1
fi

echo "Using latex.js: $LATEXJS"
echo "Fixtures: $FIXTURES_DIR"
echo "Output:   $OUTPUT_DIR"
echo ""

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning existing .latexjs.html files..."
    find "$OUTPUT_DIR" -name "*.latexjs.html" -delete
    echo ""
fi

SUCCESS=0
FAILED=0
SKIPPED=0

# Find all .tex files
while IFS= read -r texfile; do
    # Get relative path from fixtures dir
    relpath="${texfile#$FIXTURES_DIR/}"
    reldir=$(dirname "$relpath")
    basename=$(basename "$relpath" .tex)
    
    # Apply test pattern filter
    if [ -n "$TEST_PATTERN" ]; then
        if [[ ! "$relpath" == *"$TEST_PATTERN"* ]]; then
            continue
        fi
    fi
    
    # Skip math directory (latex.js may not handle well)
    if [[ "$reldir" == math/* ]]; then
        ((SKIPPED++)) || true
        continue
    fi
    
    # Create output directory
    outdir="$OUTPUT_DIR/$reldir"
    mkdir -p "$outdir"
    
    outfile="$outdir/${basename}.latexjs.html"
    
    if [ "$VERBOSE" = true ]; then
        echo "Processing: $relpath"
    fi
    
    # Generate HTML using latex.js with -b (body only)
    if "$LATEXJS" -b "$texfile" > "$outfile" 2>/dev/null; then
        ((SUCCESS++)) || true
        if [ "$VERBOSE" = true ]; then
            echo "  -> $outfile"
        fi
    else
        ((FAILED++)) || true
        if [ "$VERBOSE" = true ]; then
            echo "  FAILED: $relpath"
        fi
        rm -f "$outfile"
    fi
    
done < <(find "$FIXTURES_DIR" -name "*.tex" -type f | sort)

echo ""
echo "Summary"
echo "-------"
echo "Success: $SUCCESS"
echo "Failed:  $FAILED"
echo "Skipped: $SKIPPED"
