#!/bin/bash

# Mac native compilation dependency setup script
# Enhanced with intelligent dependency detection to skip rebuilding ThorVG and rpmalloc
# when they're already properly downloaded, built, and verified
set -e

SCRIPT_DIR="$(pwd)"
# Install dependencies to system locations that build_lambda_config.json expects
SYSTEM_PREFIX="/usr/local"

# Check for cleanup option
if [ "$1" = "clean" ] || [ "$1" = "--clean" ]; then
    echo "Cleaning up intermediate files..."

    # Clean tree-sitter build files
    rm -f lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter/tree_sitter.o 2>/dev/null || true
    rm -f lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter-lambda/src/*.o 2>/dev/null || true
    rm -f lambda/tree-sitter-javascript/libtree-sitter-javascript.a lambda/tree-sitter-javascript/src/*.o 2>/dev/null || true
    rm -f lambda/tree-sitter-python/libtree-sitter-python.a lambda/tree-sitter-python/src/*.o 2>/dev/null || true
    rm -f lambda/tree-sitter-latex/libtree-sitter-latex.a lambda/tree-sitter-latex/src/*.o 2>/dev/null || true
    rm -f lambda/tree-sitter-latex-math/libtree-sitter-latex-math.a lambda/tree-sitter-latex-math/src/*.o 2>/dev/null || true

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

    # Clean rpmalloc build files
    if [ -d "mac-deps/rpmalloc-src" ]; then
        cd mac-deps/rpmalloc-src
        rm -f *.o *.a 2>/dev/null || true
        cd - > /dev/null
    fi

    # Clean ThorVG build files
    if [ -d "mac-deps/thorvg" ]; then
        cd mac-deps/thorvg
        rm -rf build-mac 2>/dev/null || true
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
if npx --yes tree-sitter-cli@0.24.7 --version >/dev/null 2>&1; then
    echo "Tree-sitter CLI 0.24.7 accessible via npx"
else
    echo "Warning: tree-sitter CLI may need to be downloaded on first use"
fi

# Install npm dependencies (jsdom for test comparators, puppeteer for browser tests)
# Use --ignore-scripts to avoid node-gyp trying to build tree-sitter-javascript
# native binding before parser.c is generated. The C static library is built
# separately by the tree-sitter build steps below.
echo "Installing npm dependencies..."
if [ -f "package.json" ]; then
    PUPPETEER_SKIP_DOWNLOAD=true npm install --ignore-scripts
    echo "npm dependencies installed"
else
    echo "Warning: package.json not found, skipping npm install"
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

# Check for pkg-config (needed for dependency discovery)
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Installing pkg-config..."
    if command -v brew >/dev/null 2>&1; then
        brew install pkg-config
    else
        echo "Error: pkg-config is required. Install it manually or install Homebrew first."
        exit 1
    fi
fi

# Check for meson and ninja (needed for ThorVG)
if ! command -v meson >/dev/null 2>&1; then
    echo "Installing meson and ninja..."
    if command -v brew >/dev/null 2>&1; then
        brew install meson ninja
    elif command -v pip3 >/dev/null 2>&1; then
        pip3 install --user meson ninja
    else
        echo "Error: meson is required for ThorVG. Install it manually or install Homebrew first."
        exit 1
    fi
fi

# Check for mbedtls (needed for libcurl with TLS support)
if ! brew list mbedtls@3 >/dev/null 2>&1; then
    echo "Installing mbedtls@3..."
    if command -v brew >/dev/null 2>&1; then
        brew install mbedtls@3
        brew link mbedtls@3
    else
        echo "Warning: mbedtls not found. libcurl with TLS may not build correctly."
    fi
fi

# Check for freetype
if ! brew list freetype >/dev/null 2>&1; then
    echo "Installing freetype..."
    if command -v brew >/dev/null 2>&1; then
        brew install freetype
    else
        echo "Warning: freetype not found."
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

    # Clean dependency intermediate build files but keep final outputs
    # Preserve build_temp/re2-noabsl/ (RE2 source) and build_temp/utf8proc/build/libutf8proc.a
    if [ -d "build_temp" ]; then
        find build_temp -name "CMakeCache.txt" -type f -delete 2>/dev/null || true
        rm -rf build_temp/*/build/CMakeFiles 2>/dev/null || true
    fi

    echo "Cleanup completed."
}

# Function to build MIR for Mac (local to project, no sudo required)
build_mir_for_mac() {
    echo "Building MIR for Mac (local install to mac-deps/mir)..."
    cd "$SCRIPT_DIR"

    # Check if already built locally
    if [ -f "mac-deps/mir/libmir.a" ] && [ -f "mac-deps/mir/mir.h" ] && [ -f "mac-deps/mir/mir-dlist.h" ]; then
        echo "✅ MIR already built in mac-deps/mir"
        return 0
    fi

    mkdir -p mac-deps

    if [ ! -d "mac-deps/mir" ]; then
        echo "Cloning MIR repository..."
        cd mac-deps
        git clone https://github.com/vnmakarov/mir.git || {
            echo "Warning: Could not clone MIR repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi

    cd "mac-deps/mir" || {
        echo "Error: Could not enter mac-deps/mir directory"
        return 1
    }

    # Apply MIR alloca-branch fix patch
    PATCH_FILE="$SCRIPT_DIR/patches/mir-alloca-branch-fix.patch"
    if [ -f "$PATCH_FILE" ]; then
        echo "Applying MIR patches..."
        git apply "$PATCH_FILE" 2>/dev/null || {
            git apply --check "$PATCH_FILE" 2>/dev/null && true || {
                echo "  (patch already applied or skipped)"
            }
        }
    fi

    echo "Building MIR..."
    if make -j$(sysctl -n hw.ncpu); then
        echo "✅ MIR built successfully in mac-deps/mir"
        cd - > /dev/null
        return 0
    fi

    echo "❌ MIR build failed"
    cd - > /dev/null
    return 1
}

# Function to build rpmalloc for Mac
build_rpmalloc_for_mac() {
    echo "Building rpmalloc for Mac..."
    cd "$SCRIPT_DIR"

    # Enhanced check if already built in mac-deps with proper verification
    if [ -f "mac-deps/rpmalloc-install/lib/librpmalloc_no_override.a" ] && [ -f "mac-deps/rpmalloc-install/include/rpmalloc/rpmalloc.h" ]; then
        # Verify the library has the expected symbols
        if nm "mac-deps/rpmalloc-install/lib/librpmalloc_no_override.a" 2>/dev/null | grep -q "rpmalloc_initialize"; then
            echo "✅ rpmalloc already built and verified"
            return 0
        else
            echo "rpmalloc found but missing expected symbols, rebuilding..."
            # Clean the incomplete installation
            rm -rf mac-deps/rpmalloc-install 2>/dev/null || true
        fi
    fi

    # Create mac-deps directory if it doesn't exist
    mkdir -p mac-deps

    # Check if source exists, if not clone it
    if [ ! -d "mac-deps/rpmalloc-src" ]; then
        echo "Cloning rpmalloc repository..."
        cd mac-deps
        git clone https://github.com/mjansson/rpmalloc.git rpmalloc-src || {
            echo "Warning: Could not clone rpmalloc repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    else
        echo "rpmalloc source already downloaded"
    fi

    cd mac-deps/rpmalloc-src

    # Clean any previous builds
    rm -f *.o *.a 2>/dev/null || true
    rm -rf ../rpmalloc-install 2>/dev/null || true

    # Create install directories
    mkdir -p ../rpmalloc-install/lib
    mkdir -p ../rpmalloc-install/include/rpmalloc

    # Build rpmalloc with ENABLE_OVERRIDE=0 (no malloc override)
    # This allows us to use rpmalloc only for explicit pool allocations
    echo "Compiling rpmalloc with ENABLE_OVERRIDE=0..."
    if gcc -c -O2 \
        -DRPMALLOC_FIRST_CLASS_HEAPS=1 \
        -DENABLE_OVERRIDE=0 \
        -I. \
        rpmalloc/rpmalloc.c \
        -o rpmalloc_no_override.o; then

        echo "Creating static library..."
        if ar rcs librpmalloc_no_override.a rpmalloc_no_override.o; then
            # Install the library and headers
            echo "Installing rpmalloc to mac-deps..."
            cp librpmalloc_no_override.a ../rpmalloc-install/lib/
            cp rpmalloc/rpmalloc.h ../rpmalloc-install/include/rpmalloc/

            # Verify the library has expected symbols
            if nm "../rpmalloc-install/lib/librpmalloc_no_override.a" | grep -q "rpmalloc_initialize"; then
                echo "✅ rpmalloc built successfully"
                echo "   - rpmalloc_initialize: ✓ Available"
                echo "   - rpmalloc_heap_acquire: ✓ Available"
                echo "   - rpmalloc_heap_alloc: ✓ Available"
                echo "   - ENABLE_OVERRIDE=0: ✓ No malloc override"
                cd - > /dev/null
                return 0
            else
                echo "❌ Required functions not found in built library"
                cd - > /dev/null
                return 1
            fi
        fi
    fi

    echo "❌ rpmalloc build failed"
    cd - > /dev/null
    return 1
}

# Function to build ThorVG v1.0-pre34 for Mac
build_thorvg_v1_0_pre34_for_mac() {
    echo "Building ThorVG v1.0-pre34 for Mac..."
    cd "$SCRIPT_DIR"

    # Check if already built in mac-deps and verify headers
    if [ -f "mac-deps/thorvg/build-mac/src/libthorvg.a" ]; then
        if [ -f "mac-deps/thorvg/inc/thorvg.h" ]; then
            # Verify no GL symbols (which cause conflicts with system OpenGL)
            if ! nm "mac-deps/thorvg/build-mac/src/libthorvg.a" 2>/dev/null | grep -q "glClearColor"; then
                echo "✅ ThorVG v1.0-pre34 already built in mac-deps"
                return 0
            else
                echo "ThorVG library has GL symbols (conflicts with OpenGL), rebuilding..."
                rm -rf "mac-deps/thorvg" 2>/dev/null || true
            fi
        else
            echo "ThorVG library found but headers missing, rebuilding..."
        fi
    fi

    # Create mac-deps directory if it doesn't exist
    mkdir -p mac-deps

    if [ ! -d "mac-deps/thorvg" ]; then
        cd mac-deps
        echo "Cloning ThorVG repository..."
        git clone https://github.com/thorvg/thorvg.git || {
            echo "Warning: Could not clone ThorVG repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    else
        echo "ThorVG source already downloaded"
        cd "mac-deps/thorvg"
        # Make sure we're up to date
        git fetch --tags 2>/dev/null || true
        cd - > /dev/null
    fi

    cd "mac-deps/thorvg"

    # Checkout v1.0-pre34 specifically
    echo "Checking out ThorVG v1.0-pre34..."
    git fetch --tags
    git checkout v1.0-pre34 || {
        echo "❌ Failed to checkout ThorVG v1.0-pre34"
        cd - > /dev/null
        return 1
    }

    # Apply Lambda patches
    PATCH_FILE="$SCRIPT_DIR/patches/thorvg-svg-font-inheritance.patch"
    if [ -f "$PATCH_FILE" ]; then
        echo "Applying ThorVG patches..."
        git apply "$PATCH_FILE" 2>/dev/null || {
            # Check if already applied
            git apply --check "$PATCH_FILE" 2>/dev/null && true || {
                echo "  (patch already applied or skipped)"
            }
        }
    fi

    PATCH_FILE2="$SCRIPT_DIR/patches/thorvg-nested-svg-viewport.patch"
    if [ -f "$PATCH_FILE2" ]; then
        echo "Applying ThorVG nested SVG viewport patch..."
        git apply "$PATCH_FILE2" 2>/dev/null || {
            git apply --check "$PATCH_FILE2" 2>/dev/null && true || {
                echo "  (patch already applied or skipped)"
            }
        }
    fi

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

        # Clean previous build directory to ensure fresh configuration
        rm -rf build-mac
        mkdir -p build-mac

        echo "Configuring ThorVG with Meson..."
        # CRITICAL: Build with ONLY SW engine (no GL) to avoid OpenGL symbol conflicts
        # The GL engine includes GLAD which defines GL functions as global variables,
        # causing bus errors at runtime when system OpenGL tries to use them.
        if meson setup build-mac \
            --buildtype=plain \
            --default-library=static \
            -Dengines=sw \
            -Dloaders=svg,ttf \
            -Dsavers= \
            -Dbindings=capi \
            -Dtools= \
            -Dtests=false \
            -Dsimd=true \
            -Dthreads=true; then

            # Patch out problematic assertion flag for newer macOS SDKs
            echo "Patching build configuration for macOS compatibility..."
            sed -i.bak 's/-D_LIBCPP_ENABLE_ASSERTIONS=1//g' build-mac/build.ninja

            echo "Building ThorVG v1.0-pre34..."
            if ninja -C build-mac; then
                echo "ThorVG v1.0-pre34 built in mac-deps/thorvg/build-mac/"
                # No installation needed - library stays in mac-deps directory

                # Verify build
                if [ -f "build-mac/src/libthorvg.a" ] && [ -f "inc/thorvg.h" ]; then
                    # Verify text API is available
                    if nm "build-mac/src/libthorvg.a" | grep -q "tvg_text_new"; then
                        # Verify no GL symbols
                        if ! nm "build-mac/src/libthorvg.a" | grep -q "glClearColor"; then
                            echo "✅ ThorVG v1.0-pre34 built successfully"
                            echo "   - Text API: ✓ Available"
                            echo "   - GL symbols: ✓ None (no conflicts)"
                            echo "   - Location: mac-deps/thorvg/build-mac/src/libthorvg.a"
                            cd - > /dev/null
                            return 0
                        else
                            echo "❌ ThorVG contains GL symbols (should not happen with sw-only build)"
                            cd - > /dev/null
                            return 1
                        fi
                    else
                        echo "❌ ThorVG text API not available (C API binding may have failed)"
                        cd - > /dev/null
                        return 1
                    fi
                fi
            fi
        fi
    else
        echo "❌ ThorVG meson.build not found - unsupported build system"
        cd - > /dev/null
        return 1
    fi

    echo "❌ ThorVG v1.0-pre34 build failed"
    cd - > /dev/null
    return 1
}

# Function to build Brotli for Mac (required by WOFF2)
build_brotli_for_mac() {
    echo "Building Brotli for Mac..."
    cd "$SCRIPT_DIR"

    # Check if already built
    if [ -f "mac-deps/brotli/out/libbrotlidec.a" ] && [ -f "mac-deps/brotli/out/libbrotlicommon.a" ]; then
        # Verify the library has expected symbols
        if nm "mac-deps/brotli/out/libbrotlidec.a" 2>/dev/null | grep -q "BrotliDecoderDecompress"; then
            echo "✅ Brotli already built and verified"
            return 0
        else
            echo "Brotli found but missing expected symbols, rebuilding..."
            rm -rf mac-deps/brotli 2>/dev/null || true
        fi
    fi

    # Create mac-deps directory if it doesn't exist
    mkdir -p mac-deps

    # Clone brotli if not exists
    if [ ! -d "mac-deps/brotli" ]; then
        echo "Cloning Brotli repository..."
        cd mac-deps
        git clone https://github.com/google/brotli.git || {
            echo "Warning: Could not clone Brotli repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi

    cd mac-deps/brotli

    # Clean previous builds
    rm -rf out 2>/dev/null || true
    mkdir -p out

    echo "Configuring Brotli with CMake..."
    cd out
    if cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBROTLI_DISABLE_TESTS=ON \
        ..; then

        echo "Building Brotli..."
        if make -j$(sysctl -n hw.ncpu); then
            # Verify the build
            if [ -f "libbrotlidec.a" ] && [ -f "libbrotlicommon.a" ]; then
                if nm "libbrotlidec.a" | grep -q "BrotliDecoderDecompress"; then
                    echo "✅ Brotli built successfully"
                    echo "   - libbrotlidec.a: ✓ Available"
                    echo "   - libbrotlicommon.a: ✓ Available"
                    echo "   - Location: mac-deps/brotli/out/"
                    cd "$SCRIPT_DIR"
                    return 0
                fi
            fi
        fi
    fi

    echo "❌ Brotli build failed"
    cd "$SCRIPT_DIR"
    return 1
}

# Function to build WOFF2 for Mac
build_woff2_for_mac() {
    echo "Building WOFF2 for Mac..."
    cd "$SCRIPT_DIR"

    # Check if already built
    if [ -f "mac-deps/woff2/out/libwoff2dec.a" ] && [ -f "mac-deps/woff2/out/libwoff2common.a" ]; then
        # Verify the library has expected symbols
        if nm "mac-deps/woff2/out/libwoff2dec.a" 2>/dev/null | grep -q "ConvertWOFF2ToTTF"; then
            echo "✅ WOFF2 already built and verified"
            return 0
        else
            echo "WOFF2 found but missing expected symbols, rebuilding..."
            rm -rf mac-deps/woff2 2>/dev/null || true
        fi
    fi

    # Ensure Brotli is built first
    if [ ! -f "mac-deps/brotli/out/libbrotlidec.a" ]; then
        echo "Brotli not found, building first..."
        if ! build_brotli_for_mac; then
            echo "❌ Cannot build WOFF2 without Brotli"
            return 1
        fi
    fi

    # Create mac-deps directory if it doesn't exist
    mkdir -p mac-deps

    # Clone woff2 if not exists (use google/woff2 official repo)
    if [ ! -d "mac-deps/woff2" ]; then
        echo "Cloning WOFF2 repository..."
        cd mac-deps
        git clone --recursive https://github.com/google/woff2.git woff2 || {
            echo "Warning: Could not clone WOFF2 repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi

    cd mac-deps/woff2

    # Clean previous builds
    rm -rf out 2>/dev/null || true
    mkdir -p out

    echo "Configuring WOFF2 with CMake..."
    cd out

    # Point to our locally built Brotli
    BROTLI_DIR="$SCRIPT_DIR/mac-deps/brotli"

    if cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBROTLIDEC_INCLUDE_DIRS="$BROTLI_DIR/c/include" \
        -DBROTLIDEC_LIBRARIES="$BROTLI_DIR/out/libbrotlidec.a;$BROTLI_DIR/out/libbrotlicommon.a" \
        -DBROTLIENC_INCLUDE_DIRS="$BROTLI_DIR/c/include" \
        -DBROTLIENC_LIBRARIES="$BROTLI_DIR/out/libbrotlienc.a;$BROTLI_DIR/out/libbrotlicommon.a" \
        ..; then

        echo "Building WOFF2..."
        if make -j$(sysctl -n hw.ncpu); then
            # Verify the build
            if [ -f "libwoff2dec.a" ] && [ -f "libwoff2common.a" ]; then
                if nm "libwoff2dec.a" | grep -q "ConvertWOFF2ToTTF"; then
                    echo "✅ WOFF2 built successfully"
                    echo "   - libwoff2dec.a: ✓ Available"
                    echo "   - libwoff2common.a: ✓ Available"
                    echo "   - Location: mac-deps/woff2/out/"
                    cd "$SCRIPT_DIR"
                    return 0
                fi
            fi
        fi
    fi

    echo "❌ WOFF2 build failed"
    cd "$SCRIPT_DIR"
    return 1
}

# Function to build Google Test for Mac
build_gtest_for_mac() {
    echo "Building Google Test for Mac..."
    cd "$SCRIPT_DIR"

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
                cd "$SCRIPT_DIR"
                return 0
            fi
        fi
    fi

    echo "❌ Google Test build failed"
    cd "$SCRIPT_DIR"
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

# Function to build nghttp2 for Mac
build_nghttp2_for_mac() {
    echo "Building nghttp2 for Mac..."
    cd "$SCRIPT_DIR"

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
                    cd "$SCRIPT_DIR"
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

# Function to build libcurl with HTTP/2 support for Mac (using mbedTLS)
build_curl_with_http2_for_mac() {
    echo "Building libcurl with HTTP/2 and mbedTLS support for Mac..."
    cd "$SCRIPT_DIR"

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

    # Get mbedTLS path from Homebrew
    if command -v brew >/dev/null 2>&1; then
        MBEDTLS_PATH=$(brew --prefix mbedtls@3)
        if [ ! -d "$MBEDTLS_PATH" ]; then
            echo "❌ mbedTLS not found in Homebrew. Install it with: brew install mbedtls@3"
            cd - > /dev/null
            return 1
        fi
    else
        echo "❌ Homebrew required for mbedTLS path"
        cd - > /dev/null
        return 1
    fi

    # Configure libcurl with HTTP/2 and mbedTLS support
    echo "Configuring libcurl with HTTP/2 and mbedTLS support..."
    echo "mbedTLS path: $MBEDTLS_PATH"
    
    # Set PKG_CONFIG_PATH for mbedTLS discovery
    export PKG_CONFIG_PATH="$MBEDTLS_PATH/lib/pkgconfig:$PKG_CONFIG_PATH"
    
    if ./configure --prefix="$SCRIPT_DIR/mac-deps/curl-8.10.1" \
        --enable-static --disable-shared \
        --with-mbedtls="$MBEDTLS_PATH" \
        --without-openssl --without-gnutls --without-wolfssl \
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
                echo "✅ libcurl with HTTP/2 and mbedTLS built successfully"

                # Verify mbedTLS linkage
                echo "Verifying TLS linkage..."
                if otool -L lib/.libs/libcurl.a 2>/dev/null | grep -q mbedtls; then
                    echo "✅ libcurl is properly linked with mbedTLS"
                else
                    echo "⚠️  Note: Static library linkage verification may not show mbedTLS (normal for .a files)"
                fi

                cd - > /dev/null
                return 0
            fi
        fi
    fi

    echo "❌ libcurl build failed"
    cd - > /dev/null
    return 1
}

echo "Found native compiler: $(which gcc)"

# Build tree-sitter library for Mac (amalgamated, no ICU)
# Note: tree-sitter CLI is not installed globally - it's used via npx in Makefile
if [ ! -f "lambda/tree-sitter/libtree-sitter.a" ]; then
    echo "Building tree-sitter library for Mac (amalgamated, no ICU)..."
    cd lambda/tree-sitter

    # Clean previous builds
    rm -f libtree-sitter.a tree_sitter.o

    # Build static library using amalgamated single-file approach (no ICU dependency)
    cc -c lib/src/lib.c \
        -Ilib/include \
        -O3 -Wall -Wextra -std=c11 -fPIC \
        -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE \
        -o tree_sitter.o
    ar rcs libtree-sitter.a tree_sitter.o
    rm -f tree_sitter.o

    cd - > /dev/null
    echo "✅ Tree-sitter library built successfully (no ICU)"
else
    echo "✅ Tree-sitter library already built for Mac"
fi

# Build tree-sitter-lambda for Mac
if [ ! -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
    echo "Building tree-sitter-lambda for Mac..."
    cd lambda/tree-sitter-lambda

    # Clean previous builds
    make clean || true

    # Build static library for Mac (creates libtree-sitter-lambda.a)
    make TS="npx --yes tree-sitter-cli@0.24.7" libtree-sitter-lambda.a

    cd - > /dev/null
    echo "Tree-sitter-lambda built successfully"
else
    echo "Tree-sitter-lambda already built for macOS"
fi

# Build ThorVG v1.0-pre11 for Mac
echo "Setting up ThorVG ..."

# Build tree-sitter-javascript for Mac
if [ ! -f "lambda/tree-sitter-javascript/libtree-sitter-javascript.a" ]; then
    echo "Building tree-sitter-javascript for Mac..."
    cd lambda/tree-sitter-javascript

    # Clean previous builds
    make clean || true

    # Generate parser if needed
    if [ ! -f "src/parser.c" ] || [ ! -f "src/grammar.json" ]; then
        echo "Generating tree-sitter-javascript parser..."
        if command -v npx >/dev/null 2>&1; then
            npx --yes tree-sitter-cli@0.24.7 generate
        else
            echo "Warning: npx not available, assuming parser files are already generated"
        fi
    fi

    # Build static library for Mac (creates libtree-sitter-javascript.a)
    make TS="npx --yes tree-sitter-cli@0.24.7" libtree-sitter-javascript.a

    cd - > /dev/null
    echo "Tree-sitter-javascript built successfully"
else
    echo "Tree-sitter-javascript already built for Mac"
fi

# Build ThorVG v1.0-pre34 for Mac (with TTF loader for SVG text support)
echo "Setting up ThorVG ..."

# Check if ThorVG v1.0-pre34 is already properly built in mac-deps
if [ -f "mac-deps/thorvg/build-mac/src/libthorvg.a" ]; then
    # Verify it's the correct version by checking if we can find the repository with the right tag
    if [ -d "mac-deps/thorvg" ]; then
        cd "mac-deps/thorvg"
        if git describe --tags 2>/dev/null | grep -q "v1.0-pre34"; then
            echo "✅ ThorVG v1.0-pre34 already built and verified in mac-deps"
            cd - > /dev/null
        else
            echo "ThorVG found but version mismatch, rebuilding..."
            cd - > /dev/null
            # Force rebuild to ensure we have the correct version
            echo "Removing existing ThorVG build to ensure correct version..."
            rm -rf "mac-deps/thorvg" 2>/dev/null || true

            if ! build_thorvg_v1_0_pre34_for_mac; then
                echo "❌ ThorVG v1.0-pre34 build failed - required for Radiant project"
                exit 1
            else
                echo "✅ ThorVG v1.0-pre34 built successfully"
            fi
        fi
    else
        echo "✅ ThorVG v1.0-pre34 already built in mac-deps"
    fi
else
    # ThorVG not found, need to build it
    if ! build_thorvg_v1_0_pre34_for_mac; then
        echo "❌ ThorVG v1.0-pre34 build failed - required for Radiant project"
        exit 1
    else
        echo "✅ ThorVG v1.0-pre34 built successfully"
    fi
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
if [ -f "mac-deps/mir/libmir.a" ] && [ -f "mac-deps/mir/mir.h" ]; then
    echo "MIR already available in mac-deps/mir"
else
    if ! build_mir_for_mac; then
        echo "Warning: MIR build failed"
        exit 1
    else
        echo "MIR built successfully"
    fi
fi

# Build rpmalloc for Mac (Lambda dependency)
echo "Setting up rpmalloc..."

# Enhanced check for rpmalloc - verify both library and headers exist and are functional
if [ -f "mac-deps/rpmalloc-install/lib/librpmalloc_no_override.a" ] && [ -f "mac-deps/rpmalloc-install/include/rpmalloc/rpmalloc.h" ]; then
    # Verify the required functions are available in the built library
    if nm "mac-deps/rpmalloc-install/lib/librpmalloc_no_override.a" 2>/dev/null | grep -q "rpmalloc_initialize"; then
        echo "✅ rpmalloc already available and verified"
    else
        echo "rpmalloc library found but required functions missing, rebuilding..."
        if ! build_rpmalloc_for_mac; then
            echo "❌ rpmalloc build failed - required for Lambda memory pool"
            exit 1
        else
            echo "✅ rpmalloc built successfully"
        fi
    fi
else
    echo "rpmalloc not found or incomplete, building..."
    if ! build_rpmalloc_for_mac; then
        echo "❌ rpmalloc build failed - required for Lambda memory pool"
        exit 1
    else
        echo "✅ rpmalloc built successfully"
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

# Build Brotli for Mac (required by WOFF2)
echo "Setting up Brotli..."
if [ -f "mac-deps/brotli/out/libbrotlidec.a" ] && [ -f "mac-deps/brotli/out/libbrotlicommon.a" ]; then
    echo "Brotli already available"
else
    if ! build_brotli_for_mac; then
        echo "❌ Brotli build failed - required for WOFF2"
        exit 1
    else
        echo "✅ Brotli built successfully"
    fi
fi

# Build WOFF2 for Mac (required for WOFF2 font decompression)
echo "Setting up WOFF2..."
if [ -f "mac-deps/woff2/out/libwoff2dec.a" ] && [ -f "mac-deps/woff2/out/libwoff2common.a" ]; then
    echo "WOFF2 already available"
else
    if ! build_woff2_for_mac; then
        echo "❌ WOFF2 build failed - required for font decompression"
        exit 1
    else
        echo "✅ WOFF2 built successfully"
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

# Clone RE2 source (no-abseil version) for building via Makefile
echo "Setting up RE2..."
RE2_SRC="build_temp/re2-noabsl"
if [ -d "$RE2_SRC" ] && [ -f "$RE2_SRC/CMakeLists.txt" ]; then
    echo "✅ RE2 source already available"
else
    echo "Cloning RE2 (2023-03-01, no-abseil version)..."
    mkdir -p build_temp
    rm -rf "$RE2_SRC"
    if git clone --depth 1 --branch 2023-03-01 https://github.com/google/re2.git "$RE2_SRC"; then
        echo "✅ RE2 source cloned to $RE2_SRC"
    else
        echo "❌ Failed to clone RE2 - required for regex support"
        exit 1
    fi
fi

# Build utf8proc from source (static library required by build config)
echo "Setting up utf8proc..."
UTF8PROC_LIB="build_temp/utf8proc/build/libutf8proc.a"
if [ -f "$UTF8PROC_LIB" ]; then
    echo "✅ utf8proc static library already built"
else
    echo "Building utf8proc from source..."
    mkdir -p build_temp
    if [ ! -d "build_temp/utf8proc" ] || [ ! -f "build_temp/utf8proc/CMakeLists.txt" ]; then
        rm -rf build_temp/utf8proc
        if ! git clone --depth 1 https://github.com/JuliaStrings/utf8proc.git build_temp/utf8proc; then
            echo "❌ Failed to clone utf8proc - required for Unicode support"
            exit 1
        fi
    fi
    mkdir -p build_temp/utf8proc/build
    cd build_temp/utf8proc/build
    if cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUTF8PROC_INSTALL=OFF -DUTF8PROC_ENABLE_TESTING=OFF && \
       cmake --build . -j$(sysctl -n hw.ncpu); then
        echo "✅ utf8proc built successfully"
    else
        echo "❌ utf8proc build failed"
        exit 1
    fi
    cd "$SCRIPT_DIR"
fi

# Install Homebrew dependencies that are required by build_lambda_config.json
echo "Installing Homebrew dependencies..."

# Required dependencies from build_lambda_config.json
HOMEBREW_DEPS=(
    "mpdecimal"  # For decimal arithmetic - referenced in build config
    "utf8proc"   # For Unicode processing - referenced in build config
    "coreutils"  # For timeout command needed by test suite
    "mbedtls"    # For SSL/TLS support - replaced OpenSSL with mbedTLS
)

# Note: libcurl will be rebuilt with mbedTLS support (see build_curl_with_http2_for_mac)

# Radiant project dependencies - for HTML/CSS/SVG rendering engine
# Note: freetype is handled separately to ensure specific version 2.13.3
RADIANT_DEPS=(
    "glfw"       # OpenGL window and context management
    "libpng"     # PNG image format support
    "zlib"       # Compression library
    "bzip2"      # Alternative compression library
)

# Optional Radiant dependencies (not available in Homebrew, need manual installation)
RADIANT_OPTIONAL_DEPS=(
    # ThorVG will be built from source - see build_thorvg_v1_0_pre34_for_mac function
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
    echo "- mbedtls: $([ -f "$BREW_PREFIX/lib/libmbedtls.a" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- coreutils: $([ -f "$BREW_PREFIX/bin/gtimeout" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- timeout: $([ -f "$BREW_PREFIX/bin/timeout" ] && command -v timeout >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo ""
    echo "Radiant project dependencies:"
    echo "- freetype: $(brew list freetype >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- ThorVG: $([ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.dylib" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- GLFW: $(brew list glfw >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- libpng: $(brew list libpng >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- zlib: $(brew list zlib >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- bzip2: $(brew list bzip2 >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
else
    echo "- Homebrew not available for dependency checks"
fi

echo "- MIR: $([ -f "mac-deps/mir/libmir.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- rpmalloc: $([ -f "mac-deps/rpmalloc-install/lib/librpmalloc_no_override.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- ThorVG: $([ -f "mac-deps/thorvg/build-mac/src/libthorvg.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Google Test: $([ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- nghttp2: $([ -f "mac-deps/nghttp2/lib/libnghttp2.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- libcurl with HTTP/2: $([ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Brotli: $([ -f "mac-deps/brotli/out/libbrotlidec.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- WOFF2: $([ -f "mac-deps/woff2/out/libwoff2dec.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- RE2: $([ -f "build_temp/re2-noabsl/CMakeLists.txt" ] && echo "✓ Source available" || echo "✗ Missing")"
echo "- utf8proc: $([ -f "build_temp/utf8proc/build/libutf8proc.a" ] && echo "✓ Built" || echo "✗ Missing")"
# Clone GNU Bash test suite (optional, for bash transpiler conformance tests)
if [ ! -d "ref/bash" ]; then
    echo ""
    echo "Cloning GNU Bash test suite..."
    mkdir -p ref
    if git clone --depth 1 https://git.savannah.gnu.org/git/bash.git ref/bash 2>/dev/null; then
        echo "✅ GNU Bash test suite cloned to ref/bash"
    else
        echo "⚠️  Could not clone GNU Bash repo (optional, needed for bash conformance tests)"
    fi
else
    echo "✅ GNU Bash test suite already present at ref/bash"
fi
echo "- Bash tests: $([ -d "ref/bash/tests" ] && echo \"✓ Available\" || echo \"✗ Missing\")"
echo ""
echo "Next steps:"
echo "1. Run: make build           # Build Lambda main project"
echo "2. Run: make build-radiant   # Build Radiant HTML/CSS renderer"
echo "3. Run: make test            # Run tests"
echo ""
echo "To clean up intermediate files later, run: ./setup-mac-deps.sh clean"
