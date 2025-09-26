#!/bin/bash

# Mac native compilation dependency setup script
set -e

SCRIPT_DIR="$(pwd)"
# Install dependencies to system locations that build_lambda_config.json expects
SYSTEM_PREFIX="/usr/local"

# Check for cleanup option
if [ "$1" = "clean" ] || [ "$1" = "--clean" ]; then
    echo "Cleaning up intermediate files..."
    
    # Clean tree-sitter build files
    if [ -d "lambda/tree-sitter" ]; then
        cd lambda/tree-sitter
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi
    
    # Clean tree-sitter-lambda build files
    if [ -d "lambda/tree-sitter-lambda" ]; then
        cd lambda/tree-sitter-lambda
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi
    
    # Clean build directories
    rm -rf build/ build_debug/ 2>/dev/null || true
    
    # Clean object files
    find . -name "*.o" -type f -delete 2>/dev/null || true
    
    # Clean dependency build files but keep the built libraries
    if [ -d "build_temp" ]; then
        rm -rf build_temp/
    fi
    
    # Clean nghttp2 and curl build files
    if [ -d "mac-deps/nghttp2" ]; then
        cd mac-deps/nghttp2
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi
    
    if [ -d "mac-deps/curl-8.10.1" ]; then
        cd mac-deps/curl-8.10.1
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi
    
    echo "Cleanup completed."
    exit 0
fi

echo "Setting up Mac native compilation dependencies..."

# Check for required tools
check_tool() {
    local tool="$1"
    local install_cmd="$2"
    
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: $tool is required but not installed."
        echo "Install it with: $install_cmd"
        return 1
    fi
    return 0
}

# Check for Homebrew (recommended package manager for Mac)
if ! command -v brew >/dev/null 2>&1; then
    echo "Warning: Homebrew not found. Some dependencies may need manual installation."
    echo "Install Homebrew from: https://brew.sh"
fi

# Check for essential build tools
check_tool "make" "xcode-select --install" || exit 1
check_tool "gcc" "xcode-select --install" || exit 1
check_tool "git" "xcode-select --install" || exit 1

# Check for Node.js and npm (needed for tree-sitter CLI via npx)
if ! command -v node >/dev/null 2>&1; then
    echo "Installing Node.js..."
    if command -v brew >/dev/null 2>&1; then
        brew install node
    else
        echo "Error: Node.js is required for tree-sitter CLI. Install it manually or install Homebrew first."
        echo "Download from: https://nodejs.org/"
        exit 1
    fi
fi

if ! command -v npm >/dev/null 2>&1; then
    echo "Error: npm is required for tree-sitter CLI but not found with Node.js installation."
    exit 1
fi

# Verify npx can access tree-sitter CLI
echo "Verifying tree-sitter CLI access via npx..."
if npx tree-sitter-cli@0.24.7 --version >/dev/null 2>&1; then
    echo "Tree-sitter CLI 0.24.7 accessible via npx"
else
    echo "Warning: tree-sitter CLI may need to be downloaded on first use"
fi

# Check for cmake (needed for some dependencies)
if ! command -v cmake >/dev/null 2>&1; then
    echo "Installing cmake..."
    if command -v brew >/dev/null 2>&1; then
        brew install cmake
    else
        echo "Error: cmake is required. Install it manually or install Homebrew first."
        exit 1
    fi
fi

# Check for xxd (needed for binary data conversion)
if ! command -v xxd >/dev/null 2>&1; then
    echo "Error: xxd is required but not found. This is unusual on macOS as xxd is typically included by default."
    echo "You may need to install Xcode Command Line Tools: xcode-select --install"
    exit 1
else
    echo "✅ xxd already available"
fi

# Create temporary build directory
mkdir -p "build_temp"

# Function to download and extract if not exists
download_extract() {
    local name="$1"
    local url="$2"
    local archive="$3"
    
    if [ ! -d "build_temp/$name" ]; then
        echo "Downloading $name..."
        cd build_temp
        curl -L "$url" -o "$archive"
        
        case "$archive" in
            *.tar.gz) tar -xzf "$archive" ;;
            *.tar.bz2) tar -xjf "$archive" ;;
            *.tar.xz) tar -xJf "$archive" ;;
            *.zip) unzip "$archive" ;;
        esac
        
        rm "$archive"
        cd - > /dev/null
    else
        echo "$name already exists, skipping download"
    fi
}

# Function to build dependency
build_dependency() {
    local name="$1"
    local src_dir="$2"
    local build_cmd="$3"
    
    echo "Building $name for Mac..."
    
    if [ ! -d "build_temp/$src_dir" ]; then
        echo "Warning: Source directory build_temp/$src_dir not found"
        return 1
    fi
    
    cd "build_temp/$src_dir"
    
    # Set up environment for native Mac compilation
    export CC="gcc"
    export CXX="g++"
    export AR="ar"
    export STRIP="strip"
    export RANLIB="ranlib"
    export CFLAGS="-O2 -arch $(uname -m)"
    export CXXFLAGS="-O2 -arch $(uname -m)"
    export LDFLAGS="-arch $(uname -m)"
    
    # Add Homebrew paths if available
    if command -v brew >/dev/null 2>&1; then
        BREW_PREFIX=$(brew --prefix)
        export CPPFLAGS="-I$BREW_PREFIX/include $CPPFLAGS"
        export LDFLAGS="-L$BREW_PREFIX/lib $LDFLAGS"
        export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
    fi
    
    # Run the build command in a subshell to prevent exit from affecting the main script
    (eval "$build_cmd") || {
        echo "Warning: $name build failed"
        cd - > /dev/null
        return 1
    }
    
    cd - > /dev/null
    return 0
}

# Function to clean up intermediate files
cleanup_intermediate_files() {
    echo "Cleaning up intermediate files..."
    
    # Clean object files but keep static libraries
    find . -name "*.o" -type f -delete 2>/dev/null || true
    
    # Clean dependency build files but keep the built libraries
    if [ -d "build_temp" ]; then
        rm -rf build_temp/
    fi
    
    echo "Cleanup completed."
}

# Function to build lexbor submodule for Mac
build_lexbor_submodule_for_mac() {
    echo "Building lexbor submodule for Mac..."
    
    # Check if already built in submodule directory
    if [ -f "lexbor/liblexbor_static.a" ]; then
        echo "lexbor submodule already built"
        return 0
    fi
    
    # Ensure submodule is initialized
    if [ ! -f "lexbor/CMakeLists.txt" ]; then
        echo "Initializing lexbor submodule..."
        git submodule update --init lexbor || {
            echo "❌ Failed to initialize lexbor submodule"
            return 1
        }
    fi
    
    # Check if CMakeLists.txt exists
    if [ ! -f "lexbor/CMakeLists.txt" ]; then
        echo "❌ CMakeLists.txt not found in lexbor submodule directory"
        return 1
    fi
    
    cd lexbor
    
    # Clean previous builds
    rm -rf build-mac liblexbor_static.a 2>/dev/null || true
    
    # Create build directory
    mkdir -p build-mac
    cd build-mac
    
    echo "Configuring lexbor submodule with CMake..."
    if cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DLEXBOR_BUILD_SHARED=OFF \
        -DLEXBOR_BUILD_STATIC=ON \
        -DLEXBOR_BUILD_TESTS=OFF \
        -DLEXBOR_BUILD_EXAMPLES=OFF \
        -DLEXBOR_INSTALL=OFF \
        ..; then
        
        echo "Building lexbor submodule..."
        if make -j$(sysctl -n hw.ncpu); then
            # Copy the static library to the lexbor root directory where build config expects it
            if [ -f "source/liblexbor_static.a" ]; then
                cp source/liblexbor_static.a ../liblexbor_static.a
                echo "✅ lexbor submodule built successfully"
                cd - > /dev/null
                cd - > /dev/null
                return 0
            elif [ -f "liblexbor_static.a" ]; then
                cp liblexbor_static.a ../liblexbor_static.a
                echo "✅ lexbor submodule built successfully"
                cd - > /dev/null
                cd - > /dev/null
                return 0
            else
                echo "❌ lexbor static library not found after build"
                find . -name "*lexbor*" -type f | head -5
                cd - > /dev/null
                cd - > /dev/null
                return 1
            fi
        fi
    fi
    
    echo "❌ lexbor submodule build failed"
    cd - > /dev/null
    cd - > /dev/null
    return 1
}

# Function to build MIR for Mac
build_mir_for_mac() {
    echo "Building MIR for Mac..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ]; then
        echo "MIR already installed in system location"
        return 0
    fi
    
    if [ ! -d "build_temp/mir" ]; then
        cd build_temp
        echo "Cloning MIR repository..."
        git clone https://github.com/vnmakarov/mir.git || {
            echo "Warning: Could not clone MIR repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "build_temp/mir"
    
    echo "Building MIR..."
    if make -j$(sysctl -n hw.ncpu); then
        echo "Installing MIR to system location (requires sudo)..."
        # Create directories and copy files manually
        sudo mkdir -p "$SYSTEM_PREFIX/lib"
        sudo mkdir -p "$SYSTEM_PREFIX/include"
        
        # Copy the static library
        if [ -f "libmir.a" ]; then
            sudo cp "libmir.a" "$SYSTEM_PREFIX/lib/"
        fi
        
        # Copy headers
        if [ -f "mir.h" ]; then
            sudo cp "mir.h" "$SYSTEM_PREFIX/include/"
        fi
        if [ -f "mir-gen.h" ]; then
            sudo cp "mir-gen.h" "$SYSTEM_PREFIX/include/"
        fi
        
        echo "✅ MIR built successfully"
        cd - > /dev/null
        return 0
    fi
    
    echo "❌ MIR build failed"
    cd - > /dev/null
    return 1
}

# Function to build ThorVG v1.0-pre11 for Mac
build_thorvg_v1_0_pre11_for_mac() {
    echo "Building ThorVG v1.0-pre11 for Mac..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.dylib" ]; then
        echo "ThorVG v1.0-pre11 already installed in system location"
        return 0
    fi
    
    if [ ! -d "build_temp/thorvg" ]; then
        cd build_temp
        echo "Cloning ThorVG repository..."
        git clone https://github.com/thorvg/thorvg.git || {
            echo "Warning: Could not clone ThorVG repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "build_temp/thorvg"
    
    # Checkout v1.0-pre11 specifically
    echo "Checking out ThorVG v1.0-pre11..."
    git fetch --tags
    git checkout v1.0-pre11 || {
        echo "❌ Failed to checkout ThorVG v1.0-pre11"
        cd - > /dev/null
        return 1
    }
    
    # Check for meson build system
    if [ -f "meson.build" ]; then
        # Check if meson is available
        if ! command -v meson >/dev/null 2>&1; then
            echo "Installing meson via pip3..."
            if command -v pip3 >/dev/null 2>&1; then
                pip3 install --user meson ninja || {
                    echo "Failed to install meson via pip3"
                    cd - > /dev/null
                    return 1
                }
                # Add user bin to PATH if not already there
                export PATH="$HOME/.local/bin:$PATH"
            else
                echo "pip3 not found, trying Homebrew..."
                if command -v brew >/dev/null 2>&1; then
                    brew install meson ninja
                else
                    echo "Cannot install meson - no pip3 or Homebrew available"
                    cd - > /dev/null
                    return 1
                fi
            fi
        fi
        
        # Create build directory
        mkdir -p build-mac
        
        echo "Configuring ThorVG with Meson..."
        if meson setup build-mac \
            --buildtype=release \
            --default-library=both \
            --prefix="$SYSTEM_PREFIX" \
            -Dengines=sw,gl \
            -Dloaders=svg,png,jpg \
            -Dsavers= \
            -Dbindings=capi \
            -Dtools= \
            -Dtests=false \
            -Dexamples=false; then
            
            echo "Building ThorVG v1.0-pre11..."
            if meson compile -C build-mac; then
                echo "Installing ThorVG v1.0-pre11 to system location (requires sudo)..."
                if sudo meson install -C build-mac; then
                    echo "✅ ThorVG v1.0-pre11 built successfully"
                    cd - > /dev/null
                    return 0
                fi
            fi
        fi
    else
        echo "❌ ThorVG meson.build not found - unsupported build system"
        cd - > /dev/null
        return 1
    fi
    
    echo "❌ ThorVG v1.0-pre1 build failed"
    cd - > /dev/null
    return 1
}

# Function to build Google Test for Mac
build_gtest_for_mac() {
    echo "Building Google Test for Mac..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ]; then
        echo "Google Test already installed in system location"
        return 0
    fi
    
    if [ ! -d "build_temp/googletest" ]; then
        cd build_temp
        echo "Cloning Google Test repository..."
        git clone https://github.com/google/googletest.git || {
            echo "Warning: Could not clone Google Test repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "build_temp/googletest"
    
    # Create build directory
    mkdir -p build-mac
    cd build-mac
    
    echo "Configuring Google Test with CMake..."
    if cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DGTEST_FORCE_SHARED_CRT=OFF \
        -DBUILD_GMOCK=ON \
        -DINSTALL_GTEST=ON \
        -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" \
        ..; then
        
        echo "Building Google Test..."
        if make -j$(sysctl -n hw.ncpu); then
            echo "Installing Google Test to system location (requires sudo)..."
            sudo make install
            
            # Verify the build
            if [ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ]; then
                echo "✅ Google Test built successfully"
                cd - > /dev/null
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    
    echo "❌ Google Test build failed"
    cd - > /dev/null
    cd - > /dev/null
    return 1
}

# Function to setup FreeType 2.13.3 for Mac (specific version required by build config)
setup_freetype_2_13_3_for_mac() {
    echo "Setting up FreeType 2.13.3 for Mac..."
    
    # Expected paths from build config
    EXPECTED_FREETYPE_PATH="/opt/homebrew/Cellar/freetype/2.13.3"
    EXPECTED_INCLUDE_PATH="$EXPECTED_FREETYPE_PATH/include/freetype2"
    EXPECTED_LIB_PATH="$EXPECTED_FREETYPE_PATH/lib/libfreetype.a"
    
    # Check if the exact version is already installed
    if [ -d "$EXPECTED_FREETYPE_PATH" ] && [ -f "$EXPECTED_INCLUDE_PATH/ft2build.h" ] && [ -f "$EXPECTED_LIB_PATH" ]; then
        echo "✅ FreeType 2.13.3 already available at expected location"
        return 0
    fi
    
    if command -v brew >/dev/null 2>&1; then
        echo "Installing FreeType 2.13.3 via Homebrew..."
        
        # Try to install the specific version
        # First check if we need to unlink current version
        if brew list freetype >/dev/null 2>&1; then
            CURRENT_VERSION=$(brew list --versions freetype | cut -d' ' -f2)
            if [ "$CURRENT_VERSION" != "2.13.3" ]; then
                echo "Current FreeType version: $CURRENT_VERSION, need 2.13.3"
                echo "Attempting to install FreeType 2.13.3..."
                
                # Try to install the specific version using Homebrew formula
                if brew install freetype@2.13.3 2>/dev/null || brew install freetype; then
                    echo "FreeType installation completed"
                else
                    echo "❌ Failed to install FreeType via Homebrew"
                    return 1
                fi
            fi
        else
            # FreeType not installed, install it
            if brew install freetype; then
                echo "✅ FreeType installed successfully"
            else
                echo "❌ Failed to install FreeType via Homebrew"
                return 1
            fi
        fi
        
        # Check what we actually got after installation
        ACTUAL_PATH=$(find /opt/homebrew/Cellar/freetype -name "2.13.3*" -type d 2>/dev/null | head -1)
        if [ -z "$ACTUAL_PATH" ]; then
            # Try to find any freetype version and create symlink if needed
            LATEST_VERSION=$(ls /opt/homebrew/Cellar/freetype/ | sort -V | tail -1)
            ACTUAL_PATH="/opt/homebrew/Cellar/freetype/$LATEST_VERSION"
            
            if [ -d "$ACTUAL_PATH" ]; then
                echo "Found FreeType $LATEST_VERSION, creating compatibility symlink for 2.13.3..."
                ln -sf "$ACTUAL_PATH" "$EXPECTED_FREETYPE_PATH" 2>/dev/null || {
                    echo "⚠️  Warning: Could not create symlink, build config may need manual adjustment"
                    echo "Available at: $ACTUAL_PATH"
                    echo "Expected at: $EXPECTED_FREETYPE_PATH"
                    return 1
                }
            else
                echo "❌ Could not find installed FreeType"
                return 1
            fi
        fi
        
        # Final verification
        if [ -f "$EXPECTED_INCLUDE_PATH/ft2build.h" ]; then
            echo "✅ FreeType 2.13.3 setup completed successfully"
            return 0
        else
            echo "❌ FreeType headers not found at expected location: $EXPECTED_INCLUDE_PATH"
            return 1
        fi
    else
        echo "❌ Homebrew not available - cannot install FreeType"
        return 1
    fi
}

echo "Found native compiler: $(which gcc)"

# Build tree-sitter library for Mac
# Note: tree-sitter CLI is not installed globally - it's used via npx in Makefile
if [ ! -f "lambda/tree-sitter/libtree-sitter.a" ]; then
    echo "Building tree-sitter library for Mac..."
    cd lambda/tree-sitter
    
    # Clean previous builds
    make clean || true
    
    # Build static library for Mac
    make libtree-sitter.a
    
    cd - > /dev/null
    echo "Tree-sitter library built successfully"
else
    echo "Tree-sitter library already built for Mac"
fi

# Build tree-sitter-lambda for Mac
if [ ! -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
    echo "Building tree-sitter-lambda for Mac..."
    cd lambda/tree-sitter-lambda
    
    # Clean previous builds
    make clean || true
    
    # Build static library for Mac (creates libtree-sitter-lambda.a)
    make libtree-sitter-lambda.a
    
    cd - > /dev/null
    echo "Tree-sitter-lambda built successfully"
else
    echo "Tree-sitter-lambda already built for Mac"
fi

# Build lexbor submodule for Mac
echo "Setting up lexbor submodule..."
if [ -f "lexbor/liblexbor_static.a" ]; then
    echo "lexbor submodule already built"
else
    if ! build_lexbor_submodule_for_mac; then
        echo "❌ lexbor submodule build failed - required for Lambda project"
        exit 1
    else
        echo "✅ lexbor submodule built successfully"
    fi
fi

# Build ThorVG v1.0-pre1 for Mac
echo "Setting up ThorVG ..."
# Force rebuild to ensure we have the correct version (v1.0-pre1)
echo "Removing existing ThorVG installation to ensure correct version..."
sudo rm -f "$SYSTEM_PREFIX/lib/libthorvg"* 2>/dev/null || true
sudo rm -f "$SYSTEM_PREFIX/include/thorvg"* 2>/dev/null || true
sudo rm -rf "$SYSTEM_PREFIX/include/thorvg" 2>/dev/null || true
sudo rm -f "$SYSTEM_PREFIX/lib/pkgconfig/thorvg.pc" 2>/dev/null || true
rm -rf "build_temp/thorvg" 2>/dev/null || true

if ! build_thorvg_v1_0_pre11_for_mac; then
    echo "❌ ThorVG v1.0-pre11 build failed - required for Radiant project"
    exit 1
else
    echo "✅ ThorVG v1.0-pre11 built successfully"
fi

# Build Google Test for Mac
echo "Setting up Google Test..."
if [ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ]; then
    echo "Google Test already available"
else
    if ! build_gtest_for_mac; then
        echo "Warning: Google Test build failed"
        exit 1
    else
        echo "Google Test built successfully"
    fi
fi

# Build MIR for Mac (Lambda dependency)
echo "Setting up MIR..."
if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ]; then
    echo "MIR already available"
else
    if ! build_mir_for_mac; then
        echo "Warning: MIR build failed"
        exit 1
    else
        echo "MIR built successfully"
    fi
fi

# Build nghttp2 for Mac (Lambda dependency)
echo "Setting up nghttp2..."
if [ -f "mac-deps/nghttp2/lib/libnghttp2.a" ]; then
    echo "nghttp2 already available"
else
    if ! build_nghttp2_for_mac; then
        echo "Warning: nghttp2 build failed"
        exit 1
    else
        echo "nghttp2 built successfully"
    fi
fi

# Build libcurl with HTTP/2 support for Mac (Lambda dependency)
echo "Setting up libcurl with HTTP/2 support..."
if [ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ]; then
    echo "libcurl with HTTP/2 already available"
else
    if ! build_curl_with_http2_for_mac; then
        echo "Warning: libcurl with HTTP/2 build failed"
        exit 1
    else
        echo "libcurl with HTTP/2 built successfully"
    fi
fi

# Setup FreeType 2.13.3 for Mac (required by Radiant project)
echo "Setting up FreeType 2.13.3..."
if ! setup_freetype_2_13_3_for_mac; then
    echo "❌ FreeType 2.13.3 setup failed - required for Radiant project"
    exit 1
else
    echo "✅ FreeType 2.13.3 setup completed successfully"
fi

# Install Homebrew dependencies that are required by build_lambda_config.json
echo "Installing Homebrew dependencies..."

# Required dependencies from build_lambda_config.json
HOMEBREW_DEPS=(
    "mpdecimal"  # For decimal arithmetic - referenced in build config
    "utf8proc"   # For Unicode processing - referenced in build config  
    "coreutils"  # For timeout command needed by test suite
    "openssl@3"  # For SSL/TLS support - required for libcurl
    "ginac"      # For mathematical expression equivalence testing
    "libharu"    # For PDF generation - referenced in build config
)

# Radiant project dependencies - for HTML/CSS/SVG rendering engine
# Note: freetype is handled separately to ensure specific version 2.13.3
# Note: lexbor is built from submodule instead of Homebrew
RADIANT_DEPS=(
    "sdl2"       # Cross-platform multimedia library
    "sdl2_image" # SDL2 image loading support
    "glfw"       # OpenGL window and context management
    "libpng"     # PNG image format support
    "fontconfig" # Font configuration library
    "expat"      # XML parser library (dependency of fontconfig)
    "zlib"       # Compression library
    "bzip2"      # Alternative compression library
)

# Optional Radiant dependencies (not available in Homebrew, need manual installation)
RADIANT_OPTIONAL_DEPS=(
    # ThorVG will be built from source - see build_thorvg_v1_0_pre11_for_mac function
)

if command -v brew >/dev/null 2>&1; then
    # Install core Lambda dependencies
    for dep in "${HOMEBREW_DEPS[@]}"; do
        echo "Installing $dep..."
        if brew list "$dep" >/dev/null 2>&1; then
            echo "$dep already installed"
        else
            if brew install "$dep"; then
                echo "✅ $dep installed successfully"
            else
                echo "❌ Failed to install $dep"
                exit 1
            fi
        fi
    done
    
    # Install Radiant project dependencies
    echo "Installing Radiant project dependencies..."
    for dep in "${RADIANT_DEPS[@]}"; do
        echo "Installing $dep..."
        if brew list "$dep" >/dev/null 2>&1; then
            echo "$dep already installed"
        else
            if brew install "$dep"; then
                echo "✅ $dep installed successfully"
            else
                echo "❌ Failed to install $dep"
                exit 1
            fi
        fi
    done
    
    # Try to install optional dependencies, but don't fail if they're not available
    echo "Installing optional Radiant dependencies (will continue if not available)..."
    for dep in "${RADIANT_OPTIONAL_DEPS[@]}"; do
        echo "Installing optional $dep..."
        if brew list "$dep" >/dev/null 2>&1; then
            echo "$dep already installed"
        elif brew install "$dep" 2>/dev/null; then
            echo "✅ $dep installed successfully"
        else
            echo "⚠️  $dep not available in Homebrew - skipping (can be built from source if needed)"
        fi
    done
else
    echo "❌ Homebrew not available - cannot install required dependencies"
    exit 1
fi

# Set up timeout command for test suite
echo "Setting up timeout command for test suite..."
if command -v brew >/dev/null 2>&1; then
    BREW_PREFIX=$(brew --prefix)
    TIMEOUT_PATH="$BREW_PREFIX/bin/timeout"
    GNU_TIMEOUT_PATH="$BREW_PREFIX/opt/coreutils/libexec/gnubin/timeout"
    
    if [ ! -f "$TIMEOUT_PATH" ] && [ -f "$GNU_TIMEOUT_PATH" ]; then
        echo "Creating timeout symlink..."
        ln -sf "$GNU_TIMEOUT_PATH" "$TIMEOUT_PATH"
        echo "✅ timeout command linked successfully"
    elif [ -f "$TIMEOUT_PATH" ]; then
        echo "✅ timeout command already available"
    else
        echo "⚠️  Warning: timeout command not found, test suite may not work properly"
    fi
else
    echo "⚠️  Warning: Cannot set up timeout command without Homebrew"
fi

# Function to build nghttp2 for Mac
build_nghttp2_for_mac() {
    echo "Building nghttp2 for Mac..."
    
    # Check if already built
    if [ -f "mac-deps/nghttp2/lib/libnghttp2.a" ]; then
        echo "nghttp2 already built"
        return 0
    fi
    
    # Create mac-deps directory if it doesn't exist
    mkdir -p mac-deps
    
    # Download nghttp2 if not exists
    if [ ! -d "mac-deps/nghttp2" ]; then
        echo "Downloading nghttp2..."
        cd mac-deps
        git clone https://github.com/nghttp2/nghttp2.git || {
            echo "Warning: Could not clone nghttp2 repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd mac-deps/nghttp2

    # Prefer CMake if available, fallback to autotools if configure exists
    if [ -f "CMakeLists.txt" ]; then
        echo "Configuring nghttp2 with CMake..."
        mkdir -p build-mac
        cd build-mac
        if cmake -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DBUILD_STATIC_LIBS=ON \
            -DENABLE_LIB_ONLY=ON \
            -DBUILD_TESTING=OFF \
            -DCMAKE_INSTALL_PREFIX="$(pwd)/../" ..; then
            echo "Building nghttp2 (CMake)..."
            if make -j$(sysctl -n hw.ncpu); then
                echo "Installing nghttp2 (CMake)..."
                if make install; then
                    echo "✅ nghttp2 built successfully (CMake)"
                    cd - > /dev/null
                    cd - > /dev/null
                    return 0
                fi
            fi
        fi
        cd - > /dev/null
    elif [ -f "configure" ]; then
        echo "Configuring nghttp2 (autotools)..."
        if ./configure --prefix="$SCRIPT_DIR/mac-deps/nghttp2" \
            --enable-static --disable-shared \
            --disable-app --disable-hpack-tools \
            --disable-examples --disable-python-bindings \
            --disable-failmalloc --without-libxml2 \
            --without-jansson --without-zlib \
            --without-libevent-openssl --without-libcares \
            --without-openssl --without-libev \
            --without-cunit --without-jemalloc; then
            echo "Building nghttp2 (autotools)..."
            if make -j$(sysctl -n hw.ncpu); then
                echo "Installing nghttp2 (autotools)..."
                if make install; then
                    echo "✅ nghttp2 built successfully (autotools)"
                    cd - > /dev/null
                    return 0
                fi
            fi
        fi
        cd - > /dev/null
    elif [ -f "configure.ac" ]; then
        echo "Generating configure script for nghttp2..."
        if command -v autoreconf >/dev/null 2>&1; then
            autoreconf -i
            if [ -f "configure" ]; then
                echo "Configuring nghttp2 (autotools with autoreconf)..."
                if ./configure --prefix="$SCRIPT_DIR/mac-deps/nghttp2" \
                    --enable-static --disable-shared \
                    --disable-app --disable-hpack-tools \
                    --disable-examples --disable-python-bindings \
                    --disable-failmalloc --without-libxml2 \
                    --without-jansson --without-zlib \
                    --without-libevent-openssl --without-libcares \
                    --without-openssl --without-libev \
                    --without-cunit --without-jemalloc; then
                    echo "Building nghttp2 (autotools)..."
                    if make -j$(sysctl -n hw.ncpu); then
                        echo "Installing nghttp2 (autotools)..."
                        if make install; then
                            echo "✅ nghttp2 built successfully (autotools)"
                            cd - > /dev/null
                            return 0
                        fi
                    fi
                fi
            fi
        else
            echo "❌ autoreconf not found - cannot generate configure script"
        fi
        cd - > /dev/null
    else
        echo "❌ No supported build system found for nghttp2 (no CMakeLists.txt or configure)"
        cd - > /dev/null
        return 1
    fi

    echo "❌ nghttp2 build failed"
    cd - > /dev/null
    return 1
}

# Function to build libcurl with HTTP/2 support for Mac
build_curl_with_http2_for_mac() {
    echo "Building libcurl with HTTP/2 support for Mac..."
    
    # Check if already built
    if [ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ]; then
        echo "libcurl with HTTP/2 already built"
        return 0
    fi
    
    # Create mac-deps directory if it doesn't exist
    mkdir -p mac-deps
    
    # Download curl if not exists
    if [ ! -d "mac-deps/curl-8.10.1" ]; then
        echo "Downloading curl 8.10.1..."
        cd mac-deps
        curl -L "https://curl.se/download/curl-8.10.1.tar.gz" -o curl-8.10.1.tar.gz
        tar -xzf curl-8.10.1.tar.gz
        rm curl-8.10.1.tar.gz
        cd - > /dev/null
    fi
    
    cd mac-deps/curl-8.10.1
    
    # Get OpenSSL path from Homebrew
    if command -v brew >/dev/null 2>&1; then
        OPENSSL_PATH=$(brew --prefix openssl@3)
    else
        echo "❌ Homebrew required for OpenSSL path"
        cd - > /dev/null
        return 1
    fi
    
    # Configure libcurl with HTTP/2 support
    echo "Configuring libcurl with HTTP/2 support..."
    if ./configure --prefix="$SCRIPT_DIR/mac-deps/curl-8.10.1" \
        --enable-static --disable-shared \
        --with-openssl="$OPENSSL_PATH" \
        --with-nghttp2="$SCRIPT_DIR/mac-deps/nghttp2" \
        --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy \
        --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
        --disable-imap --disable-smb --disable-smtp --disable-gopher \
        --disable-mqtt --disable-manual --disable-libcurl-option \
        --disable-sspi --disable-ntlm --disable-tls-srp \
        --disable-unix-sockets --disable-cookies --disable-socketpair \
        --disable-http-auth --disable-doh --disable-mime \
        --disable-dateparse --disable-netrc --disable-progress-meter \
        --disable-alt-svc --disable-headers-api --disable-hsts \
        --without-brotli --without-zstd --without-librtmp \
        --without-libssh2 --without-libpsl --without-ngtcp2 \
        --without-nghttp3 --without-libidn2 --without-libgsasl \
        --without-quiche; then
        
        echo "Building libcurl..."
        if make -j$(sysctl -n hw.ncpu); then
            echo "Installing libcurl..."
            if make install; then
                echo "✅ libcurl with HTTP/2 built successfully"
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    
    echo "❌ libcurl build failed"
    cd - > /dev/null
    return 1
}

# Clean up intermediate files
cleanup_intermediate_files

echo "Mac native compilation setup completed!"
echo ""
echo "Built dependencies:"
echo "- Tree-sitter: $([ -f "lambda/tree-sitter/libtree-sitter.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Tree-sitter-lambda: $([ -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ] && echo "✓ Built" || echo "✗ Missing")"

# Check system locations and Homebrew
if command -v brew >/dev/null 2>&1; then
    BREW_PREFIX=$(brew --prefix)
    echo "- mpdecimal: $([ -f "$BREW_PREFIX/lib/libmpdec.a" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- utf8proc: $([ -f "$BREW_PREFIX/lib/libutf8proc.a" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- libedit: $([ -f "$BREW_PREFIX/lib/libedit.a" ] || [ -f "$BREW_PREFIX/lib/libedit.dylib" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- criterion: $([ -d "$BREW_PREFIX/Cellar/criterion" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- coreutils: $([ -f "$BREW_PREFIX/bin/gtimeout" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- timeout: $([ -f "$BREW_PREFIX/bin/timeout" ] && command -v timeout >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo ""
    echo "Radiant project dependencies:"
    echo "- lexbor (submodule): $([ -f "lexbor/liblexbor_static.a" ] && echo "✓ Built" || echo "✗ Missing")"
    echo "- freetype: $(brew list freetype >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- SDL2: $(brew list sdl2 >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- SDL2_image: $(brew list sdl2_image >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- ThorVG: $([ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.dylib" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- GLFW: $(brew list glfw >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- libpng: $(brew list libpng >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- fontconfig: $(brew list fontconfig >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- expat: $(brew list expat >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- zlib: $(brew list zlib >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- bzip2: $(brew list bzip2 >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
else
    echo "- Homebrew not available for dependency checks"
fi

echo "- MIR: $([ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- ThorVG: $([ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.dylib" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Google Test: $([ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- nghttp2: $([ -f "mac-deps/nghttp2/lib/libnghttp2.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- libcurl with HTTP/2: $([ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo ""
echo "Next steps:"
echo "1. Run: make build           # Build Lambda main project"
echo "2. Run: make build-radiant   # Build Radiant HTML/CSS renderer"
echo "3. Run: make test            # Run tests"
echo ""
echo "To clean up intermediate files later, run: ./setup-mac-deps.sh clean"
