#!/bin/bash
# test_dvi_compare.sh - Compare Lambda DVI output to reference

set -e

TEX_FILE="$1"
if [ -z "$TEX_FILE" ]; then
    echo "Usage: $0 <tex_file>"
    exit 1
fi

BASE=$(basename "$TEX_FILE" .tex)
REF_DVI="test/latex/reference/${BASE}.dvi"
OUT_DVI="/tmp/${BASE}_lambda.dvi"

echo "Testing: $BASE"
echo "========================================"

# Generate DVI with Lambda
echo "Generating Lambda DVI..."
./lambda.exe render "$TEX_FILE" -o "$OUT_DVI" 2>&1 | grep -E "(Successfully|Failed|ERROR)" || true

# Check if files exist
if [ ! -f "$REF_DVI" ]; then
    echo "ERROR: Reference DVI not found: $REF_DVI"
    exit 1
fi

if [ ! -f "$OUT_DVI" ]; then
    echo "ERROR: Lambda DVI not generated: $OUT_DVI"
    exit 1
fi

# Compare sizes
REF_SIZE=$(stat -f%z "$REF_DVI" 2>/dev/null || stat -c%s "$REF_DVI")
OUT_SIZE=$(stat -f%z "$OUT_DVI" 2>/dev/null || stat -c%s "$OUT_DVI")

echo ""
echo "File sizes:"
echo "  Reference: $REF_SIZE bytes"
echo "  Lambda:    $OUT_SIZE bytes"

# Use dvitype if available, otherwise hex comparison
if command -v dvitype >/dev/null 2>&1; then
    echo ""
    echo "Comparing with dvitype..."
    dvitype "$REF_DVI" > /tmp/ref_dvitype.txt 2>&1 || true
    dvitype "$OUT_DVI" > /tmp/out_dvitype.txt 2>&1 || true

    # Compare glyph positions
    grep "^setchar" /tmp/ref_dvitype.txt > /tmp/ref_chars.txt || true
    grep "^setchar" /tmp/out_dvitype.txt > /tmp/out_chars.txt || true

    echo "Reference characters:"
    head -20 /tmp/ref_chars.txt
    echo ""
    echo "Lambda characters:"
    head -20 /tmp/out_chars.txt
    echo ""

    if diff -q /tmp/ref_chars.txt /tmp/out_chars.txt > /dev/null 2>&1; then
        echo "✓ Character positions match!"
    else
        echo "✗ Character positions differ:"
        diff -u /tmp/ref_chars.txt /tmp/out_chars.txt | head -40
    fi
else
    echo ""
    echo "dvitype not available, using hex comparison..."
    hexdump -C "$REF_DVI" > /tmp/ref.hex
    hexdump -C "$OUT_DVI" > /tmp/out.hex

    if diff -q /tmp/ref.hex /tmp/out.hex > /dev/null 2>&1; then
        echo "✓ Files are identical!"
    else
        echo "✗ Files differ:"
        diff -u /tmp/ref.hex /tmp/out.hex | head -40
    fi
fi

echo ""
echo "========================================"
