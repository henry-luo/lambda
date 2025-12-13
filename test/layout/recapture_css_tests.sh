#!/bin/bash
# Recapture browser references only for HTML files that use test_base_style.css
# This avoids regenerating tests that don't depend on that CSS file
#
# Usage:
#   ./recapture_css_tests.sh --dry-run    # Show what would be recaptured
#   ./recapture_css_tests.sh              # Recapture only files with outdated references
#   ./recapture_css_tests.sh --force      # Force recapture all matching files

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DATA_DIR="$SCRIPT_DIR/data"
REF_DIR="$SCRIPT_DIR/reference"
CSS_FILE="$SCRIPT_DIR/scripts/gentest/test_base_style.css"
CSS_PATTERN="test_base_style.css"

echo "🔍 Scanning for HTML files that reference $CSS_PATTERN..."

# Find all HTML files that contain a reference to test_base_style.css
FILES_TO_RECAPTURE=()
while IFS= read -r -d '' file; do
    if grep -q "$CSS_PATTERN" "$file" 2>/dev/null; then
        FILES_TO_RECAPTURE+=("$file")
    fi
done < <(find "$DATA_DIR" -type f \( -name "*.html" -o -name "*.htm" \) -print0)

TOTAL=${#FILES_TO_RECAPTURE[@]}
echo "📁 Found $TOTAL HTML files that use $CSS_PATTERN"

if [ "$TOTAL" -eq 0 ]; then
    echo "✅ No files to recapture"
    exit 0
fi

# Check if --dry-run flag is passed
DRY_RUN=false
FORCE=false
for arg in "$@"; do
    case $arg in
        --dry-run)
            DRY_RUN=true
            ;;
        --force)
            FORCE=true
            ;;
    esac
done

if [ "$DRY_RUN" = true ]; then
    echo ""
    echo "📋 Files that would be recaptured (dry run):"
    for file in "${FILES_TO_RECAPTURE[@]}"; do
        rel_path="${file#$DATA_DIR/}"
        echo "  - $rel_path"
    done
    echo ""
    echo "Run without --dry-run to actually recapture these files"
    exit 0
fi

echo ""
echo "🚀 Starting recapture of $TOTAL files..."
echo ""

# Process files
SUCCESS=0
FAILED=0
SKIPPED=0

for file in "${FILES_TO_RECAPTURE[@]}"; do
    rel_path="${file#$DATA_DIR/}"
    basename=$(basename "$file" | sed 's/\.[^.]*$//')
    ref_file="$REF_DIR/$basename.json"
    
    # Skip if reference is newer than CSS file (already regenerated)
    if [ -f "$ref_file" ] && [ "$FORCE" != true ]; then
        if [ "$ref_file" -nt "$CSS_FILE" ]; then
            ((SKIPPED++))
            continue
        fi
    fi
    
    echo -n "📄 Processing: $rel_path ... "
    
    # Run the extraction
    if node "$SCRIPT_DIR/extract_browser_references.js" "$file" --force 2>/dev/null; then
        echo "✅"
        ((SUCCESS++))
    else
        echo "❌"
        ((FAILED++))
    fi
done

echo ""
echo "📊 Summary:"
echo "   ✅ Success: $SUCCESS"
echo "   ❌ Failed: $FAILED"  
echo "   ⏭️  Skipped (already exists): $SKIPPED"
echo ""
echo "🎉 Recapture complete!"
