#!/bin/bash

# Differential Binary Size Analysis for radiant.exe
# This script creates minimal builds to measure actual library contributions

echo "🔬 Differential Binary Size Analysis for radiant.exe"
echo "=================================================="

ORIGINAL_SIZE=$(stat -f "%z" radiant.exe)
echo "📏 Original binary size: $(numfmt --to=iec --suffix=B $ORIGINAL_SIZE) ($ORIGINAL_SIZE bytes)"

# Backup original config
cp build_lambda_config.json build_lambda_config.json.backup

# Function to build and measure
build_and_measure() {
    local config_name="$1"
    local description="$2"

    echo ""
    echo "🔨 Building: $description"
    echo "Config: $config_name"

    # Generate and build
    python3 utils/generate_premake.py --config "$config_name" --output premake5.mac.lua > /dev/null 2>&1
    premake5 gmake --file=premake5.mac.lua > /dev/null 2>&1
    make -C build/premake config=debug_native radiant > /dev/null 2>&1

    if [ -f "radiant.exe" ]; then
        local size=$(stat -f "%z" radiant.exe)
        local diff=$((ORIGINAL_SIZE - size))
        local percent=$(echo "scale=1; $diff * 100 / $ORIGINAL_SIZE" | bc -l)
        echo "   Size: $(numfmt --to=iec --suffix=B $size) ($size bytes)"
        echo "   Savings: $(numfmt --to=iec --suffix=B $diff) (${percent}%)"
        return $size
    else
        echo "   ❌ Build failed"
        return 0
    fi
}

# Create minimal config without heavy libraries
echo ""
echo "🧪 Creating minimal configurations..."

# Remove jemalloc (largest contributor)
cat build_lambda_config.json.backup | python3 -c "
import sys, json
config = json.load(sys.stdin)
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib != 'jemalloc']
        break
json.dump(config, sys.stdout, indent=2)
" > config_no_jemalloc.json

build_and_measure "config_no_jemalloc.json" "Without jemalloc"

# Remove lexbor (second largest)
cat build_lambda_config.json.backup | python3 -c "
import sys, json
config = json.load(sys.stdin)
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib != 'lexbor']
        break
json.dump(config, sys.stdout, indent=2)
" > config_no_lexbor.json

build_and_measure "config_no_lexbor.json" "Without lexbor"

# Remove both jemalloc and lexbor
cat build_lambda_config.json.backup | python3 -c "
import sys, json
config = json.load(sys.stdin)
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib not in ['jemalloc', 'lexbor']]
        break
json.dump(config, sys.stdout, indent=2)
" > config_no_jemalloc_lexbor.json

build_and_measure "config_no_jemalloc_lexbor.json" "Without jemalloc and lexbor"

# Remove graphics-heavy libraries
cat build_lambda_config.json.backup | python3 -c "
import sys, json
config = json.load(sys.stdin)
graphics_libs = ['freetype', 'fontconfig', 'ThorVG', 'turbojpeg', 'png', 'glfw']
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib not in graphics_libs]
        break
json.dump(config, sys.stdout, indent=2)
" > config_no_graphics.json

build_and_measure "config_no_graphics.json" "Without graphics libraries (freetype, fontconfig, ThorVG, turbojpeg, png, glfw)"

# Create core-only version
cat build_lambda_config.json.backup | python3 -c "
import sys, json
config = json.load(sys.stdin)
# Keep only essential libraries
essential_libs = ['GL', 'CoreFoundation', 'CoreVideo', 'IOKit', 'Foundation', 'CoreGraphics', 'AppKit', 'Carbon', 'iconv']
for target in config['targets']:
    if target['name'] == 'radiant':
        target['libraries'] = [lib for lib in target['libraries'] if lib in essential_libs]
        break
json.dump(config, sys.stdout, indent=2)
" > config_core_only.json

build_and_measure "config_core_only.json" "Core libraries only (system frameworks)"

# Restore original
cp build_lambda_config.json.backup build_lambda_config.json
python3 utils/generate_premake.py --config build_lambda_config.json --output premake5.mac.lua > /dev/null 2>&1
premake5 gmake --file=premake5.mac.lua > /dev/null 2>&1
make -C build/premake config=debug_native radiant > /dev/null 2>&1

echo ""
echo "✅ Analysis complete. Original configuration restored."
echo "🧹 Cleaning up temporary files..."
rm -f config_*.json

echo ""
echo "📊 Library Impact Summary:"
echo "========================="
echo "The analysis above shows the actual impact of removing specific libraries"
echo "from the radiant.exe binary. This gives a more accurate picture than"
echo "static library file sizes since:"
echo "• Dead code elimination removes unused functions"
echo "• Link-time optimization merges similar code"
echo "• Only symbols actually used contribute to final size"
