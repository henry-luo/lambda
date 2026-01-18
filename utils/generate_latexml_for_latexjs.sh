#!/bin/bash
# generate_latexml_for_latexjs.sh - Generate LaTeXML HTML references for latex.js fixtures
#
# This script processes the migrated latex.js fixtures (under test/latexml/fixtures/latexjs)
# and generates LaTeXML HTML references for comparison.
#
# Usage:
#   ./utils/generate_latexml_for_latexjs.sh [--clean] [--verbose]
#
# Output:
#   test/latexml/expected/latexjs/<category>/<name>.latexml.html

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

FIXTURES_DIR="$PROJECT_ROOT/test/latexml/fixtures/latexjs"
OUTPUT_DIR="$PROJECT_ROOT/test/latexml/expected/latexjs"

VERBOSE=false
CLEAN=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --verbose|-v)
            VERBOSE=true
            ;;
        --clean)
            CLEAN=true
            ;;
        --help|-h)
            echo "Usage: $0 [--clean] [--verbose]"
            echo "  --clean    Remove existing .latexml.html files first"
            echo "  --verbose  Show detailed output"
            exit 0
            ;;
    esac
done

# Check for latexml
LATEXML=$(which latexml 2>/dev/null || echo "")
LATEXMLPOST=$(which latexmlpost 2>/dev/null || echo "")

if [ -z "$LATEXML" ]; then
    # Try common homebrew paths
    if [ -f "/opt/homebrew/bin/latexml" ]; then
        LATEXML="/opt/homebrew/bin/latexml"
        LATEXMLPOST="/opt/homebrew/bin/latexmlpost"
    elif [ -f "/usr/local/bin/latexml" ]; then
        LATEXML="/usr/local/bin/latexml"
        LATEXMLPOST="/usr/local/bin/latexmlpost"
    else
        echo "Error: latexml not found. Install with: brew install latexml"
        exit 1
    fi
fi

echo "Using latexml: $LATEXML"
echo "Fixtures: $FIXTURES_DIR"
echo "Output: $OUTPUT_DIR"
echo ""

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning existing .latexml.html files..."
    find "$OUTPUT_DIR" -name "*.latexml.html" -delete 2>/dev/null || true
fi

# Count files
TOTAL=$(find "$FIXTURES_DIR" -name "*.tex" | wc -l | tr -d ' ')
echo "Processing $TOTAL LaTeX files..."
echo ""

SUCCESS=0
FAILED=0
SKIPPED=0

# Process each .tex file
while IFS= read -r tex_file; do
    # Get relative path from fixtures dir
    rel_path="${tex_file#$FIXTURES_DIR/}"
    
    # Build output path (same structure, but .latexml.html extension)
    html_file="$OUTPUT_DIR/${rel_path%.tex}.latexml.html"
    
    # Ensure output directory exists
    mkdir -p "$(dirname "$html_file")"
    
    # Create temp files
    TEMP_XML=$(mktemp)
    TEMP_HTML=$(mktemp)
    
    if [ "$VERBOSE" = true ]; then
        echo "Processing: $rel_path"
    fi
    
    # Check if file is marked as SKIP
    if grep -q "^% SKIP:" "$tex_file" 2>/dev/null; then
        if [ "$VERBOSE" = true ]; then
            echo "  -> Skipped (marked SKIP)"
        fi
        ((SKIPPED++)) || true
        rm -f "$TEMP_XML" "$TEMP_HTML"
        continue
    fi
    
    # latex.js fixtures are fragments, not full documents
    # We need to wrap them in a minimal document structure
    WRAPPED_TEX=$(mktemp)
    echo '\documentclass{article}' > "$WRAPPED_TEX"
    echo '\usepackage[utf8]{inputenc}' >> "$WRAPPED_TEX"
    echo '\usepackage{amsmath,amssymb}' >> "$WRAPPED_TEX"
    echo '\begin{document}' >> "$WRAPPED_TEX"
    # Skip comment lines at the start of the fixture
    grep -v "^%" "$tex_file" >> "$WRAPPED_TEX" 2>/dev/null || cat "$tex_file" >> "$WRAPPED_TEX"
    echo '' >> "$WRAPPED_TEX"
    echo '\end{document}' >> "$WRAPPED_TEX"
    
    # Run latexml (LaTeX -> XML)
    if "$LATEXML" --quiet "$WRAPPED_TEX" > "$TEMP_XML" 2>/dev/null; then
        # Run latexmlpost (XML -> HTML)
        if "$LATEXMLPOST" --format=html5 --dest="$TEMP_HTML" "$TEMP_XML" 2>/dev/null; then
            # Extract just the body content (similar to what latex.js outputs)
            # Remove DOCTYPE, html/head/body wrappers, keep content
            if python3 - "$TEMP_HTML" "$html_file" << 'PYTHON_SCRIPT'
import sys
import re

with open(sys.argv[1], 'r', encoding='utf-8') as f:
    html = f.read()

# Extract body content
body_match = re.search(r'<body[^>]*>(.*?)</body>', html, re.DOTALL | re.IGNORECASE)
if body_match:
    content = body_match.group(1).strip()
else:
    content = html

# Remove footer and everything after it
content = re.sub(r'<footer[^>]*>.*$', '', content, flags=re.DOTALL)

# Remove page wrapper divs
content = re.sub(r'^<div[^>]*class="ltx_page_main"[^>]*>\s*', '', content)
content = re.sub(r'^<div[^>]*class="ltx_page_content"[^>]*>\s*', '', content)
content = re.sub(r'\s*</div>\s*</div>\s*$', '', content)

# Remove the article wrapper and navigation if present
content = re.sub(r'<article[^>]*class="ltx_document"[^>]*>\s*', '', content)
content = re.sub(r'\s*</article>\s*$', '', content)

# Remove stray closing tags (multiple passes for nested cases)
content = re.sub(r'\s*</article>\s*', '', content)
content = re.sub(r'\s*</div>\s*$', '', content)

# Remove nav elements
content = re.sub(r'<nav[^>]*>.*?</nav>', '', content, flags=re.DOTALL)

# Remove empty divs
content = re.sub(r'<div[^>]*>\s*</div>', '', content)

# Clean up whitespace
content = re.sub(r'\n\s*\n', '\n', content)
content = content.strip()

with open(sys.argv[2], 'w', encoding='utf-8') as f:
    f.write(content)
    f.write('\n')
PYTHON_SCRIPT
            then
                ((SUCCESS++)) || true
                if [ "$VERBOSE" = true ]; then
                    echo "  -> OK"
                fi
            else
                ((FAILED++)) || true
                echo "  -> Failed to extract body: $rel_path"
            fi
        else
            ((FAILED++)) || true
            if [ "$VERBOSE" = true ]; then
                echo "  -> latexmlpost failed: $rel_path"
            fi
        fi
    else
        ((FAILED++)) || true
        if [ "$VERBOSE" = true ]; then
            echo "  -> latexml failed: $rel_path"
        fi
    fi
    
    # Cleanup
    rm -f "$TEMP_XML" "$TEMP_HTML" "$WRAPPED_TEX"
    
done < <(find "$FIXTURES_DIR" -name "*.tex" | sort)

echo ""
echo "Done!"
echo "  Success: $SUCCESS"
echo "  Failed:  $FAILED"
echo "  Skipped: $SKIPPED"
echo "  Total:   $TOTAL"
