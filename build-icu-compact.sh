#!/bin/bash
# ICU Ultra-Compact Build Script for Lambda Unicode Support
# Creates a ~2-4MB ICU library optimized for string comparison only

set -e

# Configuration
ICU_VERSION="75.1"
ICU_DIR="icu"
ICU_TARBALL="icu4c-75_1-src.tgz"
ICU_URL="https://github.com/unicode-org/icu/releases/download/release-75-1/${ICU_TARBALL}"
BUILD_DIR="$(pwd)/icu-compact-build"
INSTALL_DIR="$(pwd)/icu-compact"
BUILD_CONFIG="build_lambda_config.json"

# Create build directories
mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

echo "=== ICU Ultra-Compact Build for Lambda Unicode Support ==="
echo "ICU Version: $ICU_VERSION"
echo "Build Directory: $BUILD_DIR"
echo "Install Directory: $INSTALL_DIR"

# Download ICU if not exists
if [ ! -f "$ICU_TARBALL" ]; then
    echo "Downloading ICU $ICU_VERSION..."
    curl -L -o "$ICU_TARBALL" "$ICU_URL"
fi

# Extract ICU if not exists
if [ ! -d "$ICU_DIR" ]; then
    echo "Extracting ICU..."
    tar -xzf "$ICU_TARBALL"
fi

# Create minimal data filter for ultra-compact build
echo "Creating minimal ICU data filter from build config..."

# Check if build config exists
if [ ! -f "$BUILD_CONFIG" ]; then
    echo "Error: Build config file $BUILD_CONFIG not found!"
    exit 1
fi

# Extract ICU data_filter from build config
if command -v jq >/dev/null 2>&1; then
    echo "Using jq to extract data_filter from $BUILD_CONFIG..."
    
    # Find the ICU library entry and extract its data_filter
    ICU_DATA_FILTER=$(jq -r '.libraries[] | select(.name == "icu") | .data_filter' "$BUILD_CONFIG" 2>/dev/null)
    
    if [ "$ICU_DATA_FILTER" = "null" ] || [ -z "$ICU_DATA_FILTER" ]; then
        echo "Warning: No data_filter found for ICU in $BUILD_CONFIG, using minimal defaults"
        ICU_DATA_FILTER='{
  "localeFilter": {
    "filterType": "language",
    "includelist": ["root", "en"]
  },
  "collationFilter": {
    "filterType": "language", 
    "includelist": ["root", "en"]
  },
  "featureFilters": {
    "normalization": "include",
    "brkitr_rules": "exclude", 
    "brkitr_dictionaries": "exclude",
    "cnvalias": "exclude",
    "confusables": "exclude",
    "curr": "exclude",
    "lang": "exclude",
    "region": "exclude",
    "translit": "exclude",
    "unit": "exclude",
    "zone": "exclude",
    "misc": "exclude",
    "supplementalData": "include"
  }
}'
    fi
    
    # Write the extracted filter to the build directory
    echo "$ICU_DATA_FILTER" > "$BUILD_DIR/lambda_minimal.json"
    
else
    echo "Warning: jq not found, using hardcoded minimal data filter"
    cat > "$BUILD_DIR/lambda_minimal.json" << 'EOF'
{
  "localeFilter": {
    "filterType": "language",
    "includelist": ["root", "en"]
  },
  "collationFilter": {
    "filterType": "language", 
    "includelist": ["root", "en"]
  },
  "featureFilters": {
    "normalization": "include",
    "brkitr_rules": "exclude", 
    "brkitr_dictionaries": "exclude",
    "cnvalias": "exclude",
    "confusables": "exclude",
    "curr": "exclude",
    "lang": "exclude",
    "region": "exclude",
    "translit": "exclude",
    "unit": "exclude",
    "zone": "exclude",
    "misc": "exclude",
    "supplementalData": "include"
  }
}
EOF
fi

echo "ICU data filter written to: $BUILD_DIR/lambda_minimal.json"

# Configure ICU with ultra-compact settings
echo "Configuring ICU with ultra-compact settings..."
cd "$ICU_DIR/source"

# Clean previous build
make clean 2>/dev/null || true

# Ultra-compact configure flags
./configure \
    --prefix="$INSTALL_DIR" \
    --enable-static \
    --disable-shared \
    --disable-samples \
    --disable-tests \
    --disable-extras \
    --disable-dyload \
    --enable-tools \
    --without-samples \
    --with-data-packaging=archive \
    --disable-draft \
    --disable-renaming \
    --enable-small \
    CFLAGS="-Os -DNDEBUG -ffunction-sections -fdata-sections -DU_CHARSET_IS_UTF8=1" \
    CXXFLAGS="-Os -DNDEBUG -ffunction-sections -fdata-sections -DU_CHARSET_IS_UTF8=1" \
    LDFLAGS="-Wl,-dead_strip"

# Build ICU
echo "Building ICU with ultra-compact configuration..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Install ICU
echo "Installing ICU to $INSTALL_DIR..."
make install

# Post-build aggressive stripping
echo "Performing post-build aggressive stripping..."
cd "$INSTALL_DIR/lib"

# Strip unused symbols and debugging information
for lib in *.a; do
    if [ -f "$lib" ]; then
        echo "Stripping $lib..."
        strip --strip-unneeded --strip-debug "$lib" 2>/dev/null || \
        strip -S "$lib" 2>/dev/null || \
        echo "Warning: Could not strip $lib (continuing...)"
        
        # Remove unnecessary sections (Linux/GNU binutils)
        objcopy --remove-section=.comment --remove-section=.note "$lib" 2>/dev/null || true
    fi
done

# Create ultra-minimal combined library
echo "Creating ultra-minimal combined library..."
if [ -f libicuuc.a ] && [ -f libicudata.a ] && [ -f libicui18n.a ]; then
    # Extract object files and repack
    mkdir -p ../tmp_objs
    cd ../tmp_objs
    
    ar x ../lib/libicuuc.a
    ar x ../lib/libicudata.a  
    ar x ../lib/libicui18n.a
    
    # Create combined minimal library
    ar rcs ../lib/libicu_compact.a *.o
    ranlib ../lib/libicu_compact.a
    
    cd ..
    rm -rf tmp_objs
    
    echo "Created libicu_compact.a"
fi

cd "$BUILD_DIR/.."

# Report final sizes
echo ""
echo "=== ICU Ultra-Compact Build Complete ==="
echo "Installation directory: $INSTALL_DIR"
if [ -f "$INSTALL_DIR/lib/libicu_compact.a" ]; then
    SIZE=$(ls -lh "$INSTALL_DIR/lib/libicu_compact.a" | awk '{print $5}')
    echo "Ultra-compact ICU library size: $SIZE"
fi

echo ""
echo "Library files created:"
ls -lh "$INSTALL_DIR/lib/"*.a 2>/dev/null || echo "No static libraries found"

echo ""
echo "To use with Lambda:"
echo "export ICU_COMPACT_ROOT=$INSTALL_DIR"
echo "export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH"

echo ""
echo "Build complete! You can now build Lambda with Unicode support using:"
echo "make UNICODE_LEVEL=compact"
