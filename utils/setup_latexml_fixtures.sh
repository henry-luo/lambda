#!/bin/bash
# setup_latexml_fixtures.sh
#
# Copy test fixtures from LaTeXML for testing Lambda's LaTeX pipeline.
# Generates XML and HTML references using LaTeXML.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LATEXML_DIR="$PROJECT_ROOT/latexml"
FIXTURE_DIR="$PROJECT_ROOT/test/latexml/fixtures"
EXPECTED_DIR="$PROJECT_ROOT/test/latexml/expected"

# Check if latexml exists
if [ ! -d "$LATEXML_DIR/t" ]; then
    echo "Error: LaTeXML test directory not found at $LATEXML_DIR/t"
    exit 1
fi

# Create directories
echo "Creating test directories..."
mkdir -p "$FIXTURE_DIR"/{expansion,math,structure,ams,digestion,grouping,fonts}
mkdir -p "$EXPECTED_DIR"/{expansion,math,structure,ams,digestion,grouping,fonts}

# ============================================================================
# Copy Expansion Tests
# ============================================================================
echo "Copying expansion tests..."
EXPANSION_FILES=(
    "testexpand.tex"
    "testif.tex"
    "noexpand.tex"
    "noexpand_conditional.tex"
    "toks.tex"
    "env.tex"
    "environments.tex"
    "etex.tex"
    "for.tex"
    "ifthen.tex"
    "aftergroup.tex"
    "definedness.tex"
    "lettercase.tex"
    "meaning.tex"
    "numexpr.tex"
)

for f in "${EXPANSION_FILES[@]}"; do
    if [ -f "$LATEXML_DIR/t/expansion/$f" ]; then
        cp "$LATEXML_DIR/t/expansion/$f" "$FIXTURE_DIR/expansion/"
    fi
done

# ============================================================================
# Copy Math Tests
# ============================================================================
echo "Copying math tests..."
MATH_FILES=(
    "simplemath.tex"
    "fracs.tex"
    "testscripts.tex"
    "array.tex"
    "testover.tex"
    "arrows.tex"
    "choose.tex"
    "ambiguous_relations.tex"
    "compact_dual.tex"
    "declare.tex"
    "not.tex"
    "sampler.tex"
)

for f in "${MATH_FILES[@]}"; do
    if [ -f "$LATEXML_DIR/t/math/$f" ]; then
        cp "$LATEXML_DIR/t/math/$f" "$FIXTURE_DIR/math/"
    fi
done

# Copy simplemath.latexml if exists (config file)
if [ -f "$LATEXML_DIR/t/math/simplemath.latexml" ]; then
    cp "$LATEXML_DIR/t/math/simplemath.latexml" "$FIXTURE_DIR/math/"
fi

# ============================================================================
# Copy Structure Tests
# ============================================================================
echo "Copying structure tests..."
STRUCTURE_FILES=(
    "article.tex"
    "book.tex"
    "report.tex"
    "sec.tex"
    "itemize.tex"
    "enum.tex"
    "footnote.tex"
    "figures.tex"
    "hyperref.tex"
    "abstract.tex"
    "authors.tex"
    "columns.tex"
    "para.tex"
    "paralists.tex"
)

for f in "${STRUCTURE_FILES[@]}"; do
    if [ -f "$LATEXML_DIR/t/structure/$f" ]; then
        cp "$LATEXML_DIR/t/structure/$f" "$FIXTURE_DIR/structure/"
    fi
done

# ============================================================================
# Copy AMS Tests
# ============================================================================
echo "Copying AMS tests..."
AMS_FILES=(
    "amsdisplay.tex"
    "matrix.tex"
    "genfracs.tex"
    "sideset.tex"
    "dots.tex"
    "cd.tex"
    "mathtools.tex"
)

for f in "${AMS_FILES[@]}"; do
    if [ -f "$LATEXML_DIR/t/ams/$f" ]; then
        cp "$LATEXML_DIR/t/ams/$f" "$FIXTURE_DIR/ams/"
    fi
done

# ============================================================================
# Copy Digestion Tests
# ============================================================================
echo "Copying digestion tests..."
if [ -d "$LATEXML_DIR/t/digestion" ]; then
    cp "$LATEXML_DIR/t/digestion"/*.tex "$FIXTURE_DIR/digestion/" 2>/dev/null || true
fi

# ============================================================================
# Copy Grouping Tests
# ============================================================================
echo "Copying grouping tests..."
if [ -d "$LATEXML_DIR/t/grouping" ]; then
    cp "$LATEXML_DIR/t/grouping"/*.tex "$FIXTURE_DIR/grouping/" 2>/dev/null || true
fi

# ============================================================================
# Copy Font Tests
# ============================================================================
echo "Copying font tests..."
if [ -d "$LATEXML_DIR/t/fonts" ]; then
    cp "$LATEXML_DIR/t/fonts"/*.tex "$FIXTURE_DIR/fonts/" 2>/dev/null || true
fi

echo ""
echo "Fixture copy complete."
echo "Files copied to: $FIXTURE_DIR"
echo ""

# ============================================================================
# Generate References (optional - requires latexml installed)
# ============================================================================
if command -v latexml &> /dev/null; then
    echo "LaTeXML found. Generating reference outputs..."
    
    generate_references() {
        local category=$1
        for tex_file in "$FIXTURE_DIR/$category"/*.tex; do
            if [ -f "$tex_file" ]; then
                base=$(basename "$tex_file" .tex)
                echo "  Processing $category/$base..."
                
                # Generate XML
                latexml --dest="$EXPECTED_DIR/$category/$base.xml" "$tex_file" 2>/dev/null || true
                
                # Generate HTML (if XML was generated)
                if [ -f "$EXPECTED_DIR/$category/$base.xml" ]; then
                    latexmlpost --dest="$EXPECTED_DIR/$category/$base.html" \
                        --format=html5 \
                        "$EXPECTED_DIR/$category/$base.xml" 2>/dev/null || true
                fi
            fi
        done
    }
    
    for category in expansion math structure ams digestion grouping fonts; do
        if [ -d "$FIXTURE_DIR/$category" ]; then
            echo "Generating references for $category..."
            generate_references "$category"
        fi
    done
    
    echo ""
    echo "Reference generation complete."
    echo "Expected outputs at: $EXPECTED_DIR"
else
    echo "LaTeXML not found. Skipping reference generation."
    echo "To generate references, install LaTeXML and run:"
    echo "  $0 --generate-refs"
fi

echo ""
echo "Done!"
