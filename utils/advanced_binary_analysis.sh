#!/bin/bash

# Advanced Binary Size Analysis Script
# This script forces complete rebuilds to analyze actual library contributions

echo "🔬 Advanced Binary Size Analysis for radiant.exe"
echo "=============================================="

ORIGINAL_SIZE=$(stat -f "%z" radiant.exe)
echo "📏 Original binary size: $(numfmt --to=iec --suffix=B $ORIGINAL_SIZE) ($ORIGINAL_SIZE bytes)"

# Create a clean backup
cp build_lambda_config.json build_lambda_config.json.backup

# Function to build with forced clean and measure
build_clean_and_measure() {
    local config_name="$1"
    local description="$2"

    echo ""
    echo "🔨 Building: $description"
    echo "   Config: $config_name"

    # Force complete clean
    make clean > /dev/null 2>&1
    rm -rf build/premake/* > /dev/null 2>&1

    # Generate and build
    python3 utils/generate_premake.py --config "$config_name" --output premake5.mac.lua > /dev/null 2>&1
    premake5 gmake --file=premake5.mac.lua > /dev/null 2>&1

    echo "   Building (this may take a moment)..."
    make -C build/premake config=debug_native radiant > /dev/null 2>&1

    if [ -f "radiant.exe" ]; then
        local size=$(stat -f "%z" radiant.exe)
        local diff=$((ORIGINAL_SIZE - size))
        local percent=$(echo "scale=1; $diff * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
        echo "   📊 Size: $(numfmt --to=iec --suffix=B $size) ($size bytes)"
        echo "   💾 Savings: $(numfmt --to=iec --suffix=B $diff) (${percent}%)"

        # Return size for further analysis
        echo "$size"
    else
        echo "   ❌ Build failed"
        echo "0"
    fi
}

# Test 1: Remove jemalloc (should have significant impact)
echo ""
echo "🧪 Test 1: Removing jemalloc memory allocator"
python3 -c "
import json
with open('build_lambda_config.json.backup', 'r') as f:
    config = json.load(f)
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib != 'jemalloc']
        break
with open('config_no_jemalloc.json', 'w') as f:
    json.dump(config, f, indent=2)
"

size_no_jemalloc=$(build_clean_and_measure "config_no_jemalloc.json" "Without jemalloc")

# Test 2: Remove lexbor (should have significant impact)
echo ""
echo "🧪 Test 2: Removing lexbor HTML/CSS parser"
python3 -c "
import json
with open('build_lambda_config.json.backup', 'r') as f:
    config = json.load(f)
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib != 'lexbor']
        # Also need to remove lexbor sources
        target['source_files'] = [src for src in target['source_files'] if 'lexbor' not in src]
        break
with open('config_no_lexbor.json', 'w') as f:
    json.dump(config, f, indent=2)
"

size_no_lexbor=$(build_clean_and_measure "config_no_lexbor.json" "Without lexbor")

# Test 3: Remove all font-related libraries
echo ""
echo "🧪 Test 3: Removing font rendering libraries"
python3 -c "
import json
with open('build_lambda_config.json.backup', 'r') as f:
    config = json.load(f)
for target in config['targets']:
    if target['name'] == 'radiant':
        font_libs = ['freetype', 'fontconfig']
        target['libraries'] = [lib for lib in target['libraries'] if lib not in font_libs]
        break
with open('config_no_fonts.json', 'w') as f:
    json.dump(config, f, indent=2)
"

size_no_fonts=$(build_clean_and_measure "config_no_fonts.json" "Without font libraries")

# Test 4: Remove image processing libraries
echo ""
echo "🧪 Test 4: Removing image processing libraries"
python3 -c "
import json
with open('build_lambda_config.json.backup', 'r') as f:
    config = json.load(f)
for target in config['targets']:
    if target['name'] == 'radiant':
        img_libs = ['turbojpeg', 'png']
        target['libraries'] = [lib for lib in target['libraries'] if lib not in img_libs]
        break
with open('config_no_images.json', 'w') as f:
    json.dump(config, f, indent=2)
"

size_no_images=$(build_clean_and_measure "config_no_images.json" "Without image libraries")

# Test 5: Remove compression libraries
echo ""
echo "🧪 Test 5: Removing compression libraries"
python3 -c "
import json
with open('build_lambda_config.json.backup', 'r') as f:
    config = json.load(f)
for target in config['targets']:
    if target['name'] == 'radiant':
        compress_libs = ['zlib', 'bzip2']
        target['libraries'] = [lib for lib in target['libraries'] if lib not in compress_libs]
        break
with open('config_no_compression.json', 'w') as f:
    json.dump(config, f, indent=2)
"

size_no_compression=$(build_clean_and_measure "config_no_compression.json" "Without compression libraries")

# Test 6: Minimal build (just system frameworks)
echo ""
echo "🧪 Test 6: Minimal build with only system frameworks"
python3 -c "
import json
with open('build_lambda_config.json.backup', 'r') as f:
    config = json.load(f)
for target in config['targets']:
    if target['name'] == 'radiant':
        # Keep only system frameworks and essential libraries
        essential = ['GL', 'CoreFoundation', 'CoreVideo', 'IOKit', 'Foundation', 'CoreGraphics', 'AppKit', 'Carbon', 'iconv']
        target['libraries'] = essential
        # Keep essential source files
        essential_sources = [src for src in target['source_files'] if not any(x in src for x in ['lexbor', 'tree-sitter'])]
        target['source_files'] = essential_sources[:20]  # Keep just core files
        break
with open('config_minimal.json', 'w') as f:
    json.dump(config, f, indent=2)
"

size_minimal=$(build_clean_and_measure "config_minimal.json" "Minimal system frameworks only")

# Restore original configuration and rebuild
echo ""
echo "🔄 Restoring original configuration..."
cp build_lambda_config.json.backup build_lambda_config.json
make clean > /dev/null 2>&1
rm -rf build/premake/* > /dev/null 2>&1
make radiant > /dev/null 2>&1

# Clean up temporary configs
rm -f config_*.json

echo ""
echo "📊 BINARY SIZE ANALYSIS SUMMARY"
echo "==============================="
echo "Original size:    $(numfmt --to=iec --suffix=B $ORIGINAL_SIZE) ($ORIGINAL_SIZE bytes)"

if [ "$size_no_jemalloc" != "0" ]; then
    jemalloc_impact=$((ORIGINAL_SIZE - size_no_jemalloc))
    jemalloc_percent=$(echo "scale=1; $jemalloc_impact * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
    echo "Without jemalloc: $(numfmt --to=iec --suffix=B $size_no_jemalloc) (saves $(numfmt --to=iec --suffix=B $jemalloc_impact) / ${jemalloc_percent}%)"
fi

if [ "$size_no_lexbor" != "0" ]; then
    lexbor_impact=$((ORIGINAL_SIZE - size_no_lexbor))
    lexbor_percent=$(echo "scale=1; $lexbor_impact * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
    echo "Without lexbor:   $(numfmt --to=iec --suffix=B $size_no_lexbor) (saves $(numfmt --to=iec --suffix=B $lexbor_impact) / ${lexbor_percent}%)"
fi

if [ "$size_no_fonts" != "0" ]; then
    fonts_impact=$((ORIGINAL_SIZE - size_no_fonts))
    fonts_percent=$(echo "scale=1; $fonts_impact * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
    echo "Without fonts:    $(numfmt --to=iec --suffix=B $size_no_fonts) (saves $(numfmt --to=iec --suffix=B $fonts_impact) / ${fonts_percent}%)"
fi

if [ "$size_no_images" != "0" ]; then
    images_impact=$((ORIGINAL_SIZE - size_no_images))
    images_percent=$(echo "scale=1; $images_impact * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
    echo "Without images:   $(numfmt --to=iec --suffix=B $size_no_images) (saves $(numfmt --to=iec --suffix=B $images_impact) / ${images_percent}%)"
fi

if [ "$size_no_compression" != "0" ]; then
    comp_impact=$((ORIGINAL_SIZE - size_no_compression))
    comp_percent=$(echo "scale=1; $comp_impact * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
    echo "Without compress: $(numfmt --to=iec --suffix=B $size_no_compression) (saves $(numfmt --to=iec --suffix=B $comp_impact) / ${comp_percent}%)"
fi

if [ "$size_minimal" != "0" ]; then
    min_impact=$((ORIGINAL_SIZE - size_minimal))
    min_percent=$(echo "scale=1; $min_impact * 100 / $ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "0")
    echo "Minimal build:    $(numfmt --to=iec --suffix=B $size_minimal) (saves $(numfmt --to=iec --suffix=B $min_impact) / ${min_percent}%)"
fi

echo ""
echo "🎯 OPTIMIZATION RECOMMENDATIONS:"
echo "================================"
echo "The tests above show the actual impact of each library group on binary size."
echo "Libraries with higher impact should be considered for:"
echo "• Conditional compilation (feature flags)"
echo "• Dynamic linking instead of static"
echo "• Alternative smaller implementations"
echo "• Code elimination optimizations"
