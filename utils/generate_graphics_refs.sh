#!/bin/bash
# Generate LaTeXML HTML/SVG reference files for graphics fixtures
# Usage: ./utils/generate_graphics_refs.sh

set -e

FIXTURES_DIR="test/latex/fixtures/graphics"
EXPECTED_DIR="test/latex/expected/graphics"
TMP_DIR="/tmp/latexml_graphics"

# Create directories
mkdir -p "$EXPECTED_DIR"
mkdir -p "$TMP_DIR"

echo "Generating LaTeXML HTML references for graphics fixtures..."

# Process each .tex file in fixtures
for tex_file in "$FIXTURES_DIR"/*.tex; do
    if [ ! -f "$tex_file" ]; then
        continue
    fi
    
    base_name=$(basename "$tex_file" .tex)
    xml_file="$TMP_DIR/${base_name}.xml"
    html_file="$EXPECTED_DIR/${base_name}.html"
    
    echo "Processing: $base_name"
    
    # Step 1: LaTeX to XML
    if latexml "$tex_file" --destination="$xml_file" 2>/dev/null; then
        # Step 2: XML to HTML with SVG
        if latexmlpost "$xml_file" --destination="$html_file" --format=html5 2>/dev/null; then
            echo "  ✓ Generated: $html_file"
        else
            echo "  ✗ Failed latexmlpost: $base_name"
        fi
    else
        echo "  ✗ Failed latexml: $base_name"
    fi
done

echo ""
echo "Done. Reference files in: $EXPECTED_DIR"
