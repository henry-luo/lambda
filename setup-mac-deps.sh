#!/bin/bash

# Mac native compilation dependency setup script
# Enhanced with intelligent dependency detection to skip rebuilding ThorVG
# when they're already properly downloaded, built, and verified
set -e

SCRIPT_DIR="$(pwd)"
source "$SCRIPT_DIR/utils/setup_thorvg.sh"
source "$SCRIPT_DIR/utils/setup_build_deps.sh"
# Install dependencies to system locations that build_lambda_config.json expects
SYSTEM_PREFIX="/usr/local"
MACOS_DEPLOYMENT_TARGET="${LAMBDA_MACOS_DEPLOYMENT_TARGET:-${MACOSX_DEPLOYMENT_TARGET:-15.0}}"
export MACOSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"

mac_archive_matches_deployment_target() {
    local archive="$1"
    otool -l "$archive" 2>/dev/null | awk -v target="$MACOS_DEPLOYMENT_TARGET" '
        /minos / { seen = 1; if ($2 != target) bad = 1 }
        END { exit (seen && !bad) ? 0 : 1 }
    '
}

# Check for cleanup option
if [ "$1" = "clean" ] || [ "$1" = "--clean" ]; then
    echo "Cleaning up intermediate files..."

    # Clean tree-sitter build files
    rm -f lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter/tree_sitter.o 2>/dev/null || true
    rm -f lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter-lambda/src/*.o 2>/dev/null || true
    rm -f lambda/tree-sitter-javascript/libtree-sitter-javascript.a lambda/tree-sitter-javascript/src/*.o 2>/dev/null || true
    rm -f lambda/tree-sitter-python/libtree-sitter-python.a lambda/tree-sitter-python/src/*.o 2>/dev/null || true

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

# Select the full Xcode toolchain so debug builds use its current compiler and sanitizer runtime.
echo "Configuring Xcode developer tools..."
sudo xcodebuild -license accept
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer

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
if npx --yes tree-sitter-cli@0.25.10 --version >/dev/null 2>&1; then
    echo "Tree-sitter CLI 0.25.10 accessible via npx"
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

# Premake also compiles dependencies; install it before any source build.
if ! command -v premake5 >/dev/null 2>&1; then
    brew install premake || exit 1
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

# Check for ccache (optional but speeds up test rebuilds significantly).
# The Makefile auto-detects ccache via `which ccache` and wraps CC/CXX.
if ! command -v ccache >/dev/null 2>&1; then
    echo "Installing ccache..."
    if command -v brew >/dev/null 2>&1; then
        brew install ccache
    else
        echo "Warning: ccache not installed. Builds will be slower. Install Homebrew or run: brew install ccache"
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
    # Preserve build_temp/utf8proc/build/libutf8proc.a (RE2 is vendored in lib/re2)
    if [ -d "build_temp" ]; then
        find build_temp -name "CMakeCache.txt" -type f -delete 2>/dev/null || true
        rm -rf build_temp/*/build/CMakeFiles 2>/dev/null || true
    fi

    echo "Cleanup completed."
}

# MIR is no longer set up here: the source is vendored in-tree at lambda/mir
# (upstream + patches/mir-*.patch already applied) and built by `make build-mir`,
# which the main build targets depend on. See lambda/mir/VENDOR.md.

# Function to build ThorVG v1.0-pre34 for Mac
build_thorvg_v1_0_pre34_for_mac() {
    echo "Building ThorVG v1.0-pre34 for Mac..."
    cd "$SCRIPT_DIR"

    # Check if already built in mac-deps and verify headers
    if thorvg_archive_has_lottie "mac-deps/thorvg/build-mac/src/libthorvg.a"; then
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

    cd "$SCRIPT_DIR"
    build_premake_dependency thorvg "$SCRIPT_DIR/mac-deps/thorvg" "$SCRIPT_DIR/mac-deps/thorvg/build-mac" src || return 1
    thorvg_archive_has_lottie "$SCRIPT_DIR/mac-deps/thorvg/build-mac/src/libthorvg.a"
}

# Function to build Brotli for Mac (required by WOFF2)
build_brotli_for_mac() {
    echo "Building Brotli for Mac..."
    cd "$SCRIPT_DIR"

    # Check if already built
    if [ -f "mac-deps/brotli/out/libbrotlidec.a" ] && [ -f "mac-deps/brotli/out/libbrotlicommon.a" ]; then
        # Verify the library has expected symbols
        if nm "mac-deps/brotli/out/libbrotlidec.a" 2>/dev/null | grep -q "BrotliDecoderDecompress"; then
            if mac_archive_matches_deployment_target "mac-deps/brotli/out/libbrotlidec.a" && \
               mac_archive_matches_deployment_target "mac-deps/brotli/out/libbrotlicommon.a"; then
                echo "✅ Brotli already built and verified"
                return 0
            else
                echo "Brotli deployment target differs from macOS $MACOS_DEPLOYMENT_TARGET, rebuilding..."
            fi
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

    build_premake_dependency brotli "$SCRIPT_DIR/mac-deps/brotli" "$SCRIPT_DIR/mac-deps/brotli/out" .
}

# Stage Brotli under one prefix because curl's configure expects lib/ and include/.
stage_brotli_for_curl() {
    local brotli_prefix="$SCRIPT_DIR/mac-deps/brotli/install"

    if [ -f "$brotli_prefix/lib/libbrotlidec.a" ] && \
       [ -f "$brotli_prefix/lib/libbrotlicommon.a" ] && \
       [ -f "$brotli_prefix/include/brotli/decode.h" ]; then
        return 0
    fi

    echo "Staging Brotli for libcurl..."
    # --install-only needs this checkout's build manifest, which is absent when
    # mac-deps/brotli/out was built elsewhere; the build step is incremental.
    if ! build_premake_dependency brotli "$SCRIPT_DIR/mac-deps/brotli" "$brotli_prefix"; then
        echo "❌ Could not stage Brotli for libcurl"
        return 1
    fi

    if [ ! -f "$brotli_prefix/lib/libbrotlidec.a" ] || \
       [ ! -f "$brotli_prefix/lib/libbrotlicommon.a" ] || \
       [ ! -f "$brotli_prefix/include/brotli/decode.h" ]; then
        echo "❌ Staged Brotli is incomplete"
        return 1
    fi
}

curl_supports_web_features() {
    local curl_config="mac-deps/curl-8.10.1/lib/curl_config.h"
    [ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ] && \
        [ -f "$curl_config" ] && \
        grep -q '^#define HAVE_BROTLI 1$' "$curl_config" && \
        ! grep -q '^#define CURL_DISABLE_COOKIES 1$' "$curl_config" && \
        nm "mac-deps/curl-8.10.1/lib/libcurl.a" 2>/dev/null | \
            grep -q 'BrotliDecoderDecompress' && \
        curl_supports_mail_protocols "mac-deps/curl-8.10.1/lib/libcurl.a"
}

# lambda.io.mail drives IMAP, POP3 and SMTP through libcurl (vibe/Lambda_Demo_Mail.md ML1).
curl_supports_mail_protocols() {
    local symbols
    symbols="$(nm "$1" 2>/dev/null)" || return 1
    for handler in imaps pop3s smtps; do
        grep -q "_Curl_handler_${handler}\$" <<< "$symbols" || return 1
    done
}

# Function to build WOFF2 for Mac
# WOFF2 is compiled from lib/font/woff2 by the main Premake target.

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
        git clone --depth 1 --branch v1.17.0 https://github.com/google/googletest.git || {
            echo "Warning: Could not clone Google Test repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi

    build_premake_dependency googletest "$SCRIPT_DIR/build_temp/googletest" "$SYSTEM_PREFIX" lib yes
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

    build_premake_dependency nghttp2 "$SCRIPT_DIR/mac-deps/nghttp2" "$SCRIPT_DIR/mac-deps/nghttp2"
}

# Function to build libcurl with HTTP/2 support for Mac (using mbedTLS)
build_curl_with_http2_for_mac() {
    echo "Building libcurl with HTTP/2 and mbedTLS support for Mac..."
    cd "$SCRIPT_DIR"

    # Redirect cookies, compressed documents and the mail protocols all require
    # enabled curl features.
    if curl_supports_web_features; then
        echo "libcurl with HTTP/2, Brotli, cookies and mail protocols already built"
        return 0
    fi

    if [ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ]; then
        echo "Existing libcurl lacks required web or mail features, rebuilding..."
    fi

    if ! build_brotli_for_mac || ! stage_brotli_for_curl; then
        echo "❌ Brotli is required before building libcurl"
        return 1
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
    # libbrotlidec's static objects depend on libbrotlicommon.
    export LIBS="-lbrotlicommon${LIBS:+ $LIBS}"
    export LDFLAGS="-L$SCRIPT_DIR/mac-deps/brotli/install/lib${LDFLAGS:+ $LDFLAGS}"
    
    if ./configure --prefix="$SCRIPT_DIR/mac-deps/curl-8.10.1" \
        --enable-static --disable-shared \
        --with-mbedtls="$MBEDTLS_PATH" \
        --without-openssl --without-gnutls --without-wolfssl \
        --with-nghttp2="$SCRIPT_DIR/mac-deps/nghttp2" \
        --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy \
        --disable-dict --disable-telnet --disable-tftp \
        --disable-smb --disable-gopher \
        --disable-mqtt --disable-manual --disable-libcurl-option \
        --disable-sspi --disable-ntlm --disable-tls-srp \
        --disable-unix-sockets --enable-cookies --disable-socketpair \
        --disable-http-auth --disable-doh --disable-mime \
        --disable-dateparse --disable-netrc --disable-progress-meter \
        --disable-alt-svc --disable-headers-api --disable-hsts \
        --with-brotli="$SCRIPT_DIR/mac-deps/brotli/install" \
        --without-zstd --without-librtmp \
        --without-libssh2 --without-libpsl --without-ngtcp2 \
        --without-nghttp3 --without-libidn2 --without-libgsasl \
        --without-quiche; then

        echo "Building libcurl..."
        if make -j$(sysctl -n hw.ncpu); then
            # Headers already reside under the source-tree prefix; install only libcurl.a.
            echo "Installing libcurl static archive..."
            if make -C lib install-exec; then
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
    make TS="npx --yes tree-sitter-cli@0.25.10" libtree-sitter-lambda.a

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
            npx --yes tree-sitter-cli@0.25.10 generate --abi 14
        else
            echo "Warning: npx not available, assuming parser files are already generated"
        fi
    fi

    # Build static library for Mac (creates libtree-sitter-javascript.a)
    make TS="npx --yes tree-sitter-cli@0.25.10" libtree-sitter-javascript.a

    cd - > /dev/null
    echo "Tree-sitter-javascript built successfully"
else
    echo "Tree-sitter-javascript already built for Mac"
fi

# Build ThorVG v1.0-pre34 for Mac (with TTF loader for SVG text support)
echo "Setting up ThorVG ..."

# Check if ThorVG v1.0-pre34 is already properly built in mac-deps
if thorvg_archive_has_lottie "mac-deps/thorvg/build-mac/src/libthorvg.a"; then
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

# Build Brotli for Mac (required by WOFF2)
echo "Setting up Brotli..."
if [ -f "mac-deps/brotli/out/libbrotlidec.a" ] && \
   [ -f "mac-deps/brotli/out/libbrotlicommon.a" ] && \
   mac_archive_matches_deployment_target "mac-deps/brotli/out/libbrotlidec.a" && \
   mac_archive_matches_deployment_target "mac-deps/brotli/out/libbrotlicommon.a"; then
    echo "Brotli already available"
else
    if ! build_brotli_for_mac; then
        echo "❌ Brotli build failed - required for WOFF2"
        exit 1
    else
        echo "✅ Brotli built successfully"
    fi
fi

# Build libcurl with HTTP/2 and Brotli support for Mac (Lambda dependency)
echo "Setting up libcurl with HTTP/2 and Brotli support..."
if curl_supports_web_features; then
    echo "libcurl with HTTP/2 and Brotli already available"
else
    if ! build_curl_with_http2_for_mac; then
        echo "Warning: libcurl with HTTP/2 and Brotli build failed"
        exit 1
    else
        echo "libcurl with HTTP/2 and Brotli built successfully"
    fi
fi

# WOFF2 is part of the Lambda source build; no external archive is needed.

# RE2 is compiled with the host and tests by the JSON-owned Premake target.
if [ ! -f "lib/re2/re2/re2.cc" ]; then
    echo "❌ Vendored RE2 source missing at lib/re2 - required for regex support"
    exit 1
fi

# Build utf8proc from source (static library required by build config)
echo "Setting up utf8proc..."
UTF8PROC_LIB="build_temp/utf8proc/build/libutf8proc.a"
if [ -f "$UTF8PROC_LIB" ] && mac_archive_matches_deployment_target "$UTF8PROC_LIB"; then
    echo "✅ utf8proc static library already built"
else
    echo "Building utf8proc from source..."
    mkdir -p build_temp
    if [ ! -d "build_temp/utf8proc" ] || [ ! -f "build_temp/utf8proc/utf8proc.c" ]; then
        rm -rf build_temp/utf8proc
        if ! git clone --depth 1 https://github.com/JuliaStrings/utf8proc.git build_temp/utf8proc; then
            echo "❌ Failed to clone utf8proc - required for Unicode support"
            exit 1
        fi
    fi
    build_premake_dependency utf8proc "$SCRIPT_DIR/build_temp/utf8proc" "$SCRIPT_DIR/build_temp/utf8proc/build" . || exit 1
    cd "$SCRIPT_DIR"
fi

# Install Homebrew dependencies that are required by build_lambda_config.json
echo "Installing Homebrew dependencies..."

# Required dependencies from build_lambda_config.json
HOMEBREW_DEPS=(
    "mpdecimal"  # For decimal arithmetic - referenced in build config
    "utf8proc"   # For Unicode processing - referenced in build config
    "openjpeg"   # JPEG 2000 image samples in PDFs
    "coreutils"  # For timeout command needed by test suite
    "mbedtls"    # For SSL/TLS support - replaced OpenSSL with mbedTLS
)

# Note: libcurl will be rebuilt with mbedTLS support (see build_curl_with_http2_for_mac)

# Radiant project dependencies - for HTML/CSS/SVG rendering engine
# image codecs are built from lib/image by the Lambda build.
RADIANT_DEPS=(
    "glfw"       # OpenGL window and context management
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
    echo "- ThorVG: $([ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.dylib" ] && echo "✓ Available" || echo "✗ Missing")"
    echo "- GLFW: $(brew list glfw >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- zlib: $(brew list zlib >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
    echo "- bzip2: $(brew list bzip2 >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
else
    echo "- Homebrew not available for dependency checks"
fi

echo "- Image codecs: built from lib/image by Lambda"
echo "- MIR: vendored at lambda/mir (built by 'make build-mir')"
echo "- ThorVG: $([ -f "mac-deps/thorvg/build-mac/src/libthorvg.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Google Test: $([ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- nghttp2: $([ -f "mac-deps/nghttp2/lib/libnghttp2.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- libcurl with HTTP/2: $([ -f "mac-deps/curl-8.10.1/lib/libcurl.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Brotli: $([ -f "mac-deps/brotli/out/libbrotlidec.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- WOFF2: compiled from lib/font/woff2 by Premake"
echo "- RE2: $([ -f "lib/re2/re2/re2.cc" ] && echo "✓ Vendored (lib/re2)" || echo "✗ Missing")"
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
