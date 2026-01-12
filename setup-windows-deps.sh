#!/bin/bash

# Windows native compilation dependency setup script using MSYS2 and Clang
# This script sets up dependencies for compiling Lambda natively on Windows
set -e

SCRIPT_DIR="$(pwd)"
DEPS_DIR="win-native-deps"

# Detect MSYS2 environment
if [[ "$MSYSTEM" != "MSYS" && "$MSYSTEM" != "MINGW64" && "$MSYSTEM" != "CLANG64" ]]; then
    echo "Warning: This script is designed for MSYS2 environment."
    echo "Current MSYSTEM: ${MSYSTEM:-not set}"
    echo "Recommended environments: CLANG64 (preferred) or MINGW64"
    echo ""
    echo "To run this in MSYS2:"
    echo "  1. Open MSYS2 CLANG64 terminal (preferred for Clang)"
    echo "  2. Navigate to your project directory"
    echo "  3. Run this script"
    echo ""
    echo "Continuing anyway..."
fi

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to install MSYS2 packages
install_msys2_package() {
    local package="$1"
    local description="$2"

    # Check if package is already installed
    if pacman -Qq "$package" &>/dev/null; then
        echo "✅ $description ($package) already installed"
        return 0
    fi

    echo "Installing $description ($package)..."
    if ! pacman -S --noconfirm "$package" 2>/dev/null; then
        echo "Warning: Failed to install $package"
        return 1
    fi
    echo "✅ $description installed successfully"
    return 0
}

# Function to check pacman availability
check_pacman() {
    if ! command_exists pacman; then
        echo "Error: pacman not found. This script requires MSYS2."
        echo ""
        echo "Please install MSYS2 from: https://www.msys2.org/"
        echo "Then run this script from an MSYS2 terminal."
        exit 1
    fi
}

# Check for cleanup option
if [ "$1" = "clean" ] || [ "$1" = "--clean" ]; then
    echo "Cleaning up intermediate files..."

    # Clean tree-sitter build files
    if [ -d "lambda/tree-sitter" ]; then
        cd lambda/tree-sitter
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi

    # Clean build directories
    rm -rf build_win_native/ build/ build_debug/ 2>/dev/null || true

    # Clean object files
    find . -name "*.o" -type f -delete 2>/dev/null || true

    # Clean dependency build files but keep the built libraries
    if [ -d "$DEPS_DIR/src" ]; then
        find "$DEPS_DIR/src" -name "*.o" -type f -delete 2>/dev/null || true
        find "$DEPS_DIR/src" -name "Makefile" -type f -delete 2>/dev/null || true
        find "$DEPS_DIR/src" -name "config.status" -type f -delete 2>/dev/null || true
        find "$DEPS_DIR/src" -name "config.log" -type f -delete 2>/dev/null || true
        find "$DEPS_DIR/src" -name "*.la" -type f -delete 2>/dev/null || true

        # Clean up build directories in dependencies
        find "$DEPS_DIR/src" -type d -name "build-*" -exec rm -rf {} + 2>/dev/null || true
    fi

    echo "Cleanup completed."
    exit 0
fi

echo "Setting up Windows native compilation dependencies using MSYS2..."
echo "Target: Native Windows compilation with Clang"
echo ""

# Check MSYS2 availability
check_pacman

# Update MSYS2 package database
echo "Updating MSYS2 package database..."
pacman -Sy

# Install essential build tools
echo "Installing essential build tools..."

# Choose compiler toolchain based on environment
if [[ "$MSYSTEM" == "CLANG64" ]]; then
    COMPILER_PACKAGE="mingw-w64-clang-x86_64-clang"
    TOOLCHAIN_PREFIX="mingw-w64-clang-x86_64"
    COMPILER_NAME="Clang (CLANG64)"
elif [[ "$MSYSTEM" == "MINGW64" ]]; then
    COMPILER_PACKAGE="mingw-w64-x86_64-gcc"
    TOOLCHAIN_PREFIX="mingw-w64-x86_64"
    COMPILER_NAME="GCC (MINGW64)"
else
    # Default to Clang if environment is unclear
    COMPILER_PACKAGE="mingw-w64-clang-x86_64-clang"
    TOOLCHAIN_PREFIX="mingw-w64-clang-x86_64"
    COMPILER_NAME="Clang (default)"
fi

echo "Selected toolchain: $COMPILER_NAME"

# Core build tools
install_msys2_package "base-devel" "Base development tools (make, autotools, etc.)"
install_msys2_package "$COMPILER_PACKAGE" "C/C++ compiler"
install_msys2_package "${TOOLCHAIN_PREFIX}-cmake" "CMake build system"
install_msys2_package "${TOOLCHAIN_PREFIX}-ninja" "Ninja build system"

# Compiler cache for faster rebuilds
install_msys2_package "ccache" "Compiler cache (dramatically speeds up rebuilds)"

# Premake5 - try toolchain-specific first, fall back to base if not available
if ! install_msys2_package "${TOOLCHAIN_PREFIX}-premake" "Premake5 build system generator (required for Lambda)"; then
    # Try base premake5 package
    if ! install_msys2_package "premake" "Premake5 build system generator (base package)"; then
        echo "⚠️  Warning: Could not install premake5 via pacman"
        echo "   Will attempt manual installation..."
        NEED_MANUAL_PREMAKE=true
    fi
fi

# Note: pkgconf is already installed as a dependency of cmake, no need for separate pkg-config

# Essential libraries for Lambda
echo ""
echo "Installing Lambda dependencies..."

# Development tools
install_msys2_package "git" "Git version control"
install_msys2_package "${TOOLCHAIN_PREFIX}-gdb" "GDB debugger"
install_msys2_package "jq" "JSON processor for build script configuration parsing"
install_msys2_package "vim" "Vim editor (provides xxd binary data tool)"

# Core mathematical and text processing libraries
install_msys2_package "${TOOLCHAIN_PREFIX}-mpdecimal" "Multi-precision decimal library"
install_msys2_package "${TOOLCHAIN_PREFIX}-mbedtls" "mbedTLS - SSL/TLS library"

# Graphics and rendering libraries (for Radiant engine)
install_msys2_package "${TOOLCHAIN_PREFIX}-freetype" "FreeType font rendering"
install_msys2_package "${TOOLCHAIN_PREFIX}-glfw" "GLFW window management"
install_msys2_package "${TOOLCHAIN_PREFIX}-libpng" "PNG image library"
install_msys2_package "${TOOLCHAIN_PREFIX}-libjpeg-turbo" "JPEG image library"
install_msys2_package "${TOOLCHAIN_PREFIX}-giflib" "GIF image library"

# HTTP/networking libraries - minimal setup for libcurl only
# Note: nghttp2 removed - building minimal libcurl without HTTP/2 support

# Additional utilities
install_msys2_package "unzip" "Unzip utility"
install_msys2_package "curl" "curl for downloading"
install_msys2_package "wget" "wget for downloading"

# ThorVG build dependencies
install_msys2_package "${TOOLCHAIN_PREFIX}-meson" "Meson build system (for ThorVG)"
install_msys2_package "${TOOLCHAIN_PREFIX}-pkgconf" "pkg-config tool"

echo ""
echo "Setting up project-specific dependencies..."

# Create dependencies directory
mkdir -p "$DEPS_DIR"/{include,lib,src,bin}

# Function to build dependency from source
build_dependency() {
    local name="$1"
    local src_dir="$2"
    local build_cmd="$3"

    echo "Building $name for Windows native..."

    if [ ! -d "$DEPS_DIR/src/$src_dir" ]; then
        echo "Warning: Source directory $DEPS_DIR/src/$src_dir not found"
        return 1
    fi

    cd "$DEPS_DIR/src/$src_dir"

    # Set up native Windows compilation environment
    if [[ "$MSYSTEM" == "CLANG64" ]]; then
        export CC="clang"
        export CXX="clang++"
        export AR="llvm-ar"
        export RANLIB="llvm-ranlib"
        export STRIP="llvm-strip"
    else
        export CC="gcc"
        export CXX="g++"
        export AR="ar"
        export RANLIB="ranlib"
        export STRIP="strip"
    fi

    # Windows-specific flags for native compilation
    export CFLAGS="-O2 -D_WIN32 -DWINDOWS -D_GNU_SOURCE"
    export CXXFLAGS="-O2 -std=c++17 -D_WIN32 -DWINDOWS -D_GNU_SOURCE"
    export LDFLAGS=""

    # Run the build command
    (eval "$build_cmd") || {
        echo "Warning: $name build failed"
        cd - > /dev/null
        return 1
    }

    cd - > /dev/null
    return 0
}

# Function to manually install premake5 if not available via pacman
install_premake5_manually() {
    echo "Installing premake5 manually..."

    # Check if already installed
    if command_exists premake5; then
        echo "✅ premake5 already available"
        return 0
    fi

    # Determine download URL based on system
    PREMAKE_VERSION="5.0.0-beta2"
    PREMAKE_ARCHIVE="premake-${PREMAKE_VERSION}-windows.zip"
    PREMAKE_URL="https://github.com/premake/premake-core/releases/download/v${PREMAKE_VERSION}/${PREMAKE_ARCHIVE}"

    echo "Downloading premake5 from GitHub..."
    mkdir -p "$DEPS_DIR/bin"
    cd "$DEPS_DIR/bin"

    # Download premake5
    if command_exists curl; then
        if ! curl -L -f "$PREMAKE_URL" -o "$PREMAKE_ARCHIVE"; then
            echo "Error: Failed to download premake5"
            cd - > /dev/null
            return 1
        fi
    elif command_exists wget; then
        if ! wget --no-check-certificate "$PREMAKE_URL" -O "$PREMAKE_ARCHIVE"; then
            echo "Error: Failed to download premake5"
            cd - > /dev/null
            return 1
        fi
    else
        echo "Error: Neither curl nor wget available"
        cd - > /dev/null
        return 1
    fi

    # Extract premake5
    echo "Extracting premake5..."
    if ! unzip -o "$PREMAKE_ARCHIVE"; then
        echo "Error: Failed to extract premake5"
        rm -f "$PREMAKE_ARCHIVE"
        cd - > /dev/null
        return 1
    fi

    rm "$PREMAKE_ARCHIVE"

    # Make it executable
    chmod +x premake5.exe

    # Add to PATH for current session
    export PATH="$SCRIPT_DIR/$DEPS_DIR/bin:$PATH"

    # Verify installation
    if [ -f "premake5.exe" ]; then
        echo "✅ premake5 installed manually to $DEPS_DIR/bin/"
        echo "   Note: You may need to add this to your PATH permanently:"
        echo "   export PATH=\"\$PWD/$DEPS_DIR/bin:\$PATH\""
        cd - > /dev/null
        return 0
    else
        echo "Error: premake5.exe not found after extraction"
        cd - > /dev/null
        return 1
    fi
}

echo ""
echo "🔧 Building dependencies from source..."
echo "Note: This script will check for parallel directories first:"
echo "  • ../mir (for MIR JIT compiler)"
echo "  • ../utf8proc (for Unicode normalization)"
echo "If parallel directories are not found, dependencies will be downloaded."
echo "Note: Tree-sitter libraries are managed directly under lambda/ directory."
echo "Note: ThorVG must be installed separately (see above)."
echo ""

# Install premake5 manually if not available
if [ "$NEED_MANUAL_PREMAKE" = "true" ]; then
    if ! install_premake5_manually; then
        echo "Error: Failed to install premake5"
        echo "Please install premake5 manually and add it to PATH"
        exit 1
    fi
fi

# Build tree-sitter libraries for Windows
echo "Building tree-sitter libraries for Windows..."

# Build tree-sitter library
if [ ! -f "lambda/tree-sitter/libtree-sitter.a" ]; then
    echo "Building tree-sitter library for Windows..."
    cd lambda/tree-sitter

    # Clean previous builds
    make clean || true

    # Build static library for Windows
    if [[ "$MSYSTEM" == "CLANG64" ]]; then
        CC="clang" AR="llvm-ar" make libtree-sitter.a
    else
        CC="gcc" AR="ar" make libtree-sitter.a
    fi

    if [ -f "libtree-sitter.a" ]; then
        echo "✅ Tree-sitter library built successfully"
    else
        echo "❌ Tree-sitter library build failed"
        cd - > /dev/null
        exit 1
    fi

    cd - > /dev/null
else
    echo "✅ Tree-sitter library already built for Windows"
fi

# Build tree-sitter-lambda
if [ ! -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
    echo "Building tree-sitter-lambda for Windows..."
    cd lambda/tree-sitter-lambda

    # Clean previous builds
    make clean || true

    # Build static library for Windows (creates libtree-sitter-lambda.a)
    if [[ "$MSYSTEM" == "CLANG64" ]]; then
        CC="clang" AR="llvm-ar" make libtree-sitter-lambda.a
    else
        CC="gcc" AR="ar" make libtree-sitter-lambda.a
    fi

    if [ -f "libtree-sitter-lambda.a" ]; then
        echo "✅ Tree-sitter-lambda built successfully"
    else
        echo "❌ Tree-sitter-lambda build failed"
        cd - > /dev/null
        exit 1
    fi

    cd - > /dev/null
else
    echo "✅ Tree-sitter-lambda already built for Windows"
fi

# Build tree-sitter-javascript
if [ ! -f "lambda/tree-sitter-javascript/libtree-sitter-javascript.a" ]; then
    echo "Building tree-sitter-javascript for Windows..."
    cd lambda/tree-sitter-javascript

    # Clean previous builds
    make clean || true

    # Build static library for Windows
    if [[ "$MSYSTEM" == "CLANG64" ]]; then
        CC="clang" AR="llvm-ar" make libtree-sitter-javascript.a
    else
        CC="gcc" AR="ar" make libtree-sitter-javascript.a
    fi

    if [ -f "libtree-sitter-javascript.a" ]; then
        echo "✅ Tree-sitter-javascript built successfully"
    else
        echo "❌ Tree-sitter-javascript build failed"
        cd - > /dev/null
        exit 1
    fi

    cd - > /dev/null
else
    echo "✅ Tree-sitter-javascript already built for Windows"
fi

# Build tree-sitter-latex
if [ ! -f "lambda/tree-sitter-latex/libtree-sitter-latex.a" ]; then
    echo "Building tree-sitter-latex for Windows..."
    cd lambda/tree-sitter-latex

    # Clean previous builds
    make clean || true

    # Build static library for Windows
    if [[ "$MSYSTEM" == "CLANG64" ]]; then
        CC="clang" AR="llvm-ar" make libtree-sitter-latex.a
    else
        CC="gcc" AR="ar" make libtree-sitter-latex.a
    fi

    if [ -f "libtree-sitter-latex.a" ]; then
        echo "✅ Tree-sitter-latex built successfully"
    else
        echo "❌ Tree-sitter-latex build failed"
        cd - > /dev/null
        exit 1
    fi

    cd - > /dev/null
else
    echo "✅ Tree-sitter-latex already built for Windows"
fi

# Function to download and extract if not exists
download_extract() {
    local name="$1"
    local url="$2"
    local archive="$3"

    if [ ! -d "$DEPS_DIR/src/$name" ]; then
        echo "Downloading $name..."
        cd "$DEPS_DIR/src"

        if command_exists curl; then
            curl -L "$url" -o "$archive"
        elif command_exists wget; then
            wget --no-check-certificate "$url" -O "$archive"
        else
            echo "Error: Neither curl nor wget available for download"
            cd - > /dev/null
            return 1
        fi

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

# Function to build minimal static libcurl for Windows (HTTP/HTTPS only)
build_minimal_static_libcurl() {
    echo "Building minimal static libcurl for Windows native (HTTP/HTTPS only)..."

    # Check if already built
    if [ -f "$DEPS_DIR/lib/libcurl.a" ]; then
        echo "Minimal static libcurl already built"
        return 0
    fi

    # Try multiple curl versions if the primary one fails
    CURL_VERSIONS=("8.10.1" "8.9.1" "8.8.0")
    CURL_SUCCESS=false

    for CURL_VERSION in "${CURL_VERSIONS[@]}"; do
        CURL_DIR="curl-$CURL_VERSION"

        if [ -d "$DEPS_DIR/src/$CURL_DIR" ]; then
            echo "Found existing curl directory: $CURL_DIR"
            CURL_SUCCESS=true
            break
        fi

        echo "Attempting to download curl $CURL_VERSION..."
        cd "$DEPS_DIR/src"

        CURL_URL="https://curl.se/download/$CURL_DIR.tar.gz"
        CURL_ARCHIVE="$CURL_DIR.tar.gz"

        # Try downloading with curl first (more reliable in MSYS), then wget as fallback
        DOWNLOAD_SUCCESS=false

        if command_exists curl; then
            echo "Downloading with curl from: $CURL_URL"
            if curl -L -f --connect-timeout 30 --max-time 300 "$CURL_URL" -o "$CURL_ARCHIVE"; then
                DOWNLOAD_SUCCESS=true
            else
                echo "curl download failed for version $CURL_VERSION"
            fi
        fi

        if [ "$DOWNLOAD_SUCCESS" = "false" ] && command_exists wget; then
            echo "Downloading with wget from: $CURL_URL (fallback)"
            if wget --timeout=30 --tries=3 --no-check-certificate "$CURL_URL" -O "$CURL_ARCHIVE"; then
                DOWNLOAD_SUCCESS=true
            else
                echo "wget download failed for version $CURL_VERSION"
            fi
        fi

        if [ "$DOWNLOAD_SUCCESS" = "false" ]; then
            echo "⚠️  Failed to download curl $CURL_VERSION, trying next version..."
            rm -f "$CURL_ARCHIVE"
            cd - > /dev/null
            continue
        fi

        # Verify the downloaded file exists and has reasonable size
        if [ ! -f "$CURL_ARCHIVE" ]; then
            echo "⚠️  Downloaded file $CURL_ARCHIVE not found for version $CURL_VERSION"
            cd - > /dev/null
            continue
        fi

        ARCHIVE_SIZE=$(stat -c%s "$CURL_ARCHIVE" 2>/dev/null || wc -c < "$CURL_ARCHIVE" 2>/dev/null || echo "0")
        if [ "$ARCHIVE_SIZE" -lt 1000000 ]; then  # Less than 1MB is suspicious
            echo "⚠️  Downloaded file $CURL_ARCHIVE is too small ($ARCHIVE_SIZE bytes) for version $CURL_VERSION"
            echo "This might be an error page or incomplete download"
            rm -f "$CURL_ARCHIVE"
            cd - > /dev/null
            continue
        fi

        echo "✅ Downloaded curl $CURL_VERSION successfully ($ARCHIVE_SIZE bytes)"
        echo "Extracting $CURL_ARCHIVE..."

        if tar -xzf "$CURL_ARCHIVE"; then
            rm "$CURL_ARCHIVE"
            echo "✅ Extraction successful for curl $CURL_VERSION"
            CURL_SUCCESS=true
            cd - > /dev/null
            break
        else
            echo "❌ Error: Failed to extract $CURL_ARCHIVE for version $CURL_VERSION"
            rm -f "$CURL_ARCHIVE"
            cd - > /dev/null
            continue
        fi
    done

    if [ "$CURL_SUCCESS" = "false" ]; then
        echo "❌ Error: Failed to download any curl version"
        echo "Tried versions: ${CURL_VERSIONS[*]}"
        echo "Please check your internet connection and try again"
        return 1
    fi

    # Find the successfully downloaded curl directory
    for CURL_VERSION in "${CURL_VERSIONS[@]}"; do
        if [ -d "$DEPS_DIR/src/curl-$CURL_VERSION" ]; then
            CURL_DIR="curl-$CURL_VERSION"
            break
        fi
    done

    echo "Using curl directory: $CURL_DIR"
    cd "$DEPS_DIR/src/$CURL_DIR"

    # Clean any previous builds
    echo "Cleaning previous builds..."
    make distclean 2>/dev/null || true

    # Set up environment for Windows compilation
    if [[ "$MSYSTEM" == "CLANG64" ]]; then
        export CC="clang"
        export CXX="clang++"
        export AR="llvm-ar"
        export RANLIB="llvm-ranlib"
        export STRIP="llvm-strip"
    else
        export CC="gcc"
        export CXX="g++"
        export AR="ar"
        export RANLIB="ranlib"
        export STRIP="strip"
    fi

    export CFLAGS="-O2 -DNDEBUG -D_WIN32 -DWINDOWS -DCURL_STATICLIB"
    export CXXFLAGS="-O2 -DNDEBUG -D_WIN32 -DWINDOWS -DCURL_STATICLIB"
    export LDFLAGS=""

    # Configure with bare minimum features - absolutely no SSH, IDN, HTTP2, or advanced protocols
    echo "Configuring libcurl with minimal options (HTTP/HTTPS only)..."
    if ./configure \
        --prefix="$SCRIPT_DIR/$DEPS_DIR" \
        --enable-static \
        --disable-shared \
        --with-schannel \
        --disable-http2 \
        --without-nghttp2 \
        --without-nghttp3 \
        --without-ngtcp2 \
        --without-libssh2 \
        --without-libssh \
        --without-libidn2 \
        --without-libidn \
        --without-libpsl \
        --without-brotli \
        --without-zstd \
        --without-winidn \
        --disable-ldap \
        --disable-ldaps \
        --disable-rtsp \
        --disable-ftp \
        --disable-ftps \
        --disable-file \
        --disable-dict \
        --disable-telnet \
        --disable-tftp \
        --disable-pop3 \
        --disable-imap \
        --disable-smtp \
        --disable-gopher \
        --disable-smb \
        --disable-mqtt \
        --disable-manual \
        --disable-libcurl-option \
        --disable-sspi \
        --disable-crypto-auth \
        --disable-ntlm \
        --disable-tls-srp \
        --disable-unix-sockets \
        --disable-cookies \
        --disable-socketpair \
        --disable-http-auth \
        --disable-doh \
        --disable-mime \
        --disable-dateparse \
        --disable-netrc \
        --disable-progress-meter \
        --disable-dnsshuffle \
        --disable-alt-svc; then

        echo "Configuration complete. Building..."
        if make -j4; then
            echo "Installing to win-native-deps..."
            if make install; then
                echo "✅ Minimal static libcurl built successfully"

                # Verify the build doesn't contain problematic dependencies
                echo "Verifying minimal build..."

                # Check for SSH objects
                if ar -t "$SCRIPT_DIR/$DEPS_DIR/lib/libcurl.a" | grep -i ssh; then
                    echo "WARNING: SSH objects found in libcurl.a"
                else
                    echo "✓ No SSH objects found"
                fi

                # Check for IDN objects
                if ar -t "$SCRIPT_DIR/$DEPS_DIR/lib/libcurl.a" | grep -i idn; then
                    echo "WARNING: IDN objects found in libcurl.a"
                else
                    echo "✓ No IDN objects found"
                fi

                # Check for HTTP2 objects
                if ar -t "$SCRIPT_DIR/$DEPS_DIR/lib/libcurl.a" | grep -i http2; then
                    echo "INFO: HTTP2 objects found (but should not cause linking issues)"
                else
                    echo "✓ No HTTP2 objects found"
                fi

                cd - > /dev/null
                return 0
            fi
        fi
    fi

    echo "❌ Minimal libcurl build failed"
    cd - > /dev/null
    return 1
}

# Build MIR (JIT compiler)
if [ ! -f "$DEPS_DIR/lib/libmir.a" ]; then
    echo "Building MIR for Windows native..."

    # Check for parallel directory first (preferred)
    MIR_SRC=""
    if [ -d "../mir" ]; then
        echo "Using parallel mir directory..."
        MIR_SRC="../mir"
    elif [ -d "$DEPS_DIR/src/mir" ]; then
        echo "Using previously downloaded mir..."
        MIR_SRC="$DEPS_DIR/src/mir"
    else
        # Download MIR if needed
        mkdir -p "$DEPS_DIR/src"
        cd "$DEPS_DIR/src"
        echo "Cloning MIR repository..."
        git clone https://github.com/vnmakarov/mir.git || {
            echo "Warning: Could not clone MIR repository"
            cd - > /dev/null
        }
        cd - > /dev/null
        if [ -d "$DEPS_DIR/src/mir" ]; then
            MIR_SRC="$DEPS_DIR/src/mir"
        fi
    fi

    if [ -n "$MIR_SRC" ] && [ -d "$MIR_SRC" ]; then
        echo "Building MIR from: $MIR_SRC"
        cd "$MIR_SRC"

        # Clean previous builds
        make clean 2>/dev/null || true

        # Build MIR for native Windows
        echo "Building MIR..."

        # Always use CLANG64 tools since we installed them
        CC="/clang64/bin/clang.exe" \
        AR="/clang64/bin/llvm-ar.exe" \
        CFLAGS="-O2 -DNDEBUG -fPIC" \
        make

        # Copy built libraries and headers
        if [ -f "libmir.a" ]; then
            if [[ "$MIR_SRC" == "../mir" ]]; then
                cp libmir.a "../Lambda/$DEPS_DIR/lib/"
            else
                cp libmir.a "../../$DEPS_DIR/lib/"
            fi
            echo "✅ MIR built successfully"
        else
            echo "⚠️  MIR build may have issues"
        fi

        # Copy headers
        if [[ "$MIR_SRC" == "../mir" ]]; then
            mkdir -p "../Lambda/$DEPS_DIR/include"
            HEADER_DIR="../Lambda/$DEPS_DIR/include"
        else
            mkdir -p "../../$DEPS_DIR/include"
            HEADER_DIR="../../$DEPS_DIR/include"
        fi

        for header in mir.h mir-gen.h mir-varr.h mir-dlist.h mir-hash.h mir-htab.h mir-alloc.h mir-bitmap.h mir-code-alloc.h; do
            if [ -f "$header" ]; then
                cp "$header" "$HEADER_DIR/"
            fi
        done

        cd - > /dev/null
    fi
else
    echo "✅ MIR already built for Windows native"
fi

# Build utf8proc (Unicode normalization library)
if [ ! -f "$DEPS_DIR/lib/libutf8proc.a" ]; then
    echo "Building utf8proc for Windows native..."

    # Check for parallel directory first (preferred)
    UTF8PROC_SRC=""
    if [ -d "../utf8proc" ]; then
        echo "Using parallel utf8proc directory..."
        UTF8PROC_SRC="../utf8proc"
    elif [ -d "$DEPS_DIR/src/utf8proc" ]; then
        echo "Using previously downloaded utf8proc..."
        UTF8PROC_SRC="$DEPS_DIR/src/utf8proc"
    else
        # Download utf8proc if needed
        mkdir -p "$DEPS_DIR/src"
        cd "$DEPS_DIR/src"
        echo "Cloning utf8proc repository..."
        git clone https://github.com/JuliaStrings/utf8proc.git || {
            echo "Warning: Could not clone utf8proc repository"
            cd - > /dev/null
        }
        cd - > /dev/null
        if [ -d "$DEPS_DIR/src/utf8proc" ]; then
            UTF8PROC_SRC="$DEPS_DIR/src/utf8proc"
        fi
    fi

    if [ -n "$UTF8PROC_SRC" ] && [ -d "$UTF8PROC_SRC" ]; then
        echo "Building utf8proc from: $UTF8PROC_SRC"
        cd "$UTF8PROC_SRC"

        # Clean previous builds
        make clean 2>/dev/null || true

        # Build utf8proc for native Windows
        echo "Building utf8proc..."

        # Use proper compiler based on MSYS2 environment
        if [[ "$MSYSTEM" == "CLANG64" ]]; then
            CC_BIN="clang"
            AR_BIN="llvm-ar"
        else
            CC_BIN="gcc"
            AR_BIN="ar"
        fi

        # Build only static library with UTF8PROC_STATIC define
        CC="$CC_BIN" \
        AR="$AR_BIN" \
        CFLAGS="-O2 -DNDEBUG -fPIC -DUTF8PROC_STATIC -D_WIN32 -DWINDOWS" \
        make libutf8proc.a || {
            echo "Error: Failed to build utf8proc static library"
            cd - > /dev/null
            exit 1
        }

        # Copy library and headers
        if [[ "$UTF8PROC_SRC" == "../utf8proc" ]]; then
            mkdir -p "../Lambda/$DEPS_DIR/lib"
            mkdir -p "../Lambda/$DEPS_DIR/include"
            LIB_DIR="../Lambda/$DEPS_DIR/lib"
            HEADER_DIR="../Lambda/$DEPS_DIR/include"
        else
            mkdir -p "../../$DEPS_DIR/lib"
            mkdir -p "../../$DEPS_DIR/include"
            LIB_DIR="../../$DEPS_DIR/lib"
            HEADER_DIR="../../$DEPS_DIR/include"
        fi

        # Copy the static library
        if [ -f "libutf8proc.a" ]; then
            cp "libutf8proc.a" "$LIB_DIR/"
            echo "✅ utf8proc library copied"
        else
            echo "⚠️  utf8proc library not found after build"
        fi

        # Copy headers
        if [ -f "utf8proc.h" ]; then
            cp "utf8proc.h" "$HEADER_DIR/"
            echo "✅ utf8proc headers copied"
        else
            echo "⚠️  utf8proc headers not found"
        fi

        cd - > /dev/null
    fi
else
    echo "✅ utf8proc already built for Windows native"
fi

# Build zlog (logging library) - REMOVED: No longer a dependency
# zlog has been removed as a dependency from Lambda

# Build ThorVG v1.0-pre34 (vector graphics library for SVG rendering, with TTF loader for text support)
build_thorvg() {
    echo "Building ThorVG v1.0-pre34 for Windows native..."

    # Check if already built and verify no GL symbols
    if [ -f "$DEPS_DIR/lib/libthorvg.a" ]; then
        if [ -f "$DEPS_DIR/include/thorvg.h" ]; then
            # Verify no GL symbols (which cause conflicts with system OpenGL)
            if ! nm "$DEPS_DIR/lib/libthorvg.a" 2>/dev/null | grep -q "glClearColor"; then
                echo "✅ ThorVG already built and verified"
                return 0
            else
                echo "ThorVG library has GL symbols (conflicts with OpenGL), rebuilding..."
                rm -f "$DEPS_DIR/lib/libthorvg.a" 2>/dev/null || true
            fi
        else
            echo "ThorVG library found but headers missing, rebuilding..."
        fi
    fi

    # Check for existing ThorVG source
    THORVG_SRC=""
    if [ -d "../thorvg" ]; then
        echo "Using parallel thorvg directory..."
        THORVG_SRC="../thorvg"
    elif [ -d "$DEPS_DIR/src/thorvg" ]; then
        echo "Using previously downloaded thorvg..."
        THORVG_SRC="$DEPS_DIR/src/thorvg"
    else
        # Clone ThorVG repository
        mkdir -p "$DEPS_DIR/src"
        cd "$DEPS_DIR/src"
        echo "Cloning ThorVG repository..."
        # Use v1.0-pre34 for latest features and TTF loader support
        if ! git clone --depth 1 --branch v1.0-pre34 https://github.com/thorvg/thorvg.git; then
            echo "Warning: Could not clone ThorVG repository"
            cd - > /dev/null
            return 1
        fi
        cd - > /dev/null
        THORVG_SRC="$DEPS_DIR/src/thorvg"
    fi

    if [ -n "$THORVG_SRC" ] && [ -d "$THORVG_SRC" ]; then
        echo "Building ThorVG from: $THORVG_SRC"
        cd "$THORVG_SRC"

        # Ensure we're on the right version
        echo "Checking out ThorVG v1.0-pre34..."
        git fetch --tags 2>/dev/null || true
        git checkout v1.0-pre34 2>/dev/null || true

        # Set up environment for Windows compilation
        if [[ "$MSYSTEM" == "CLANG64" ]]; then
            export CC="clang"
            export CXX="clang++"
            export AR="llvm-ar"
            export RANLIB="llvm-ranlib"
        else
            export CC="gcc"
            export CXX="g++"
            export AR="ar"
            export RANLIB="ranlib"
        fi

        export CFLAGS="-O2 -DNDEBUG -D_WIN32 -DWINDOWS"
        export CXXFLAGS="-O2 -std=c++17 -DNDEBUG -D_WIN32 -DWINDOWS"

        # Clean previous builds
        rm -rf builddir 2>/dev/null || true

        # Configure ThorVG with Meson
        # CRITICAL: Build with ONLY SW engine (no GL) to avoid OpenGL symbol conflicts
        # The GL engine includes GLAD which defines GL functions as global variables,
        # causing bus errors at runtime when system OpenGL tries to use them.
        echo "Configuring ThorVG with Meson (SW engine only)..."
        if ! meson setup builddir \
            --prefix="$SCRIPT_DIR/$DEPS_DIR" \
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
            echo "Error: ThorVG meson configuration failed"
            cd - > /dev/null
            return 1
        fi

        # Build ThorVG
        echo "Building ThorVG v1.0-pre34 (this may take a few minutes)..."
        if ! ninja -C builddir; then
            echo "Error: ThorVG build failed"
            cd - > /dev/null
            return 1
        fi

        # Install ThorVG
        echo "Installing ThorVG to win-native-deps..."
        if ! ninja -C builddir install; then
            echo "Error: ThorVG installation failed"
            cd - > /dev/null
            return 1
        fi

        # Verify installation
        if [ -f "$SCRIPT_DIR/$DEPS_DIR/lib/libthorvg.a" ] && [ -f "$SCRIPT_DIR/$DEPS_DIR/include/thorvg.h" ]; then
            # Verify text API is available
            if nm "$SCRIPT_DIR/$DEPS_DIR/lib/libthorvg.a" | grep -q "tvg_text_new"; then
                # Verify no GL symbols
                if ! nm "$SCRIPT_DIR/$DEPS_DIR/lib/libthorvg.a" | grep -q "glClearColor"; then
                    echo "✅ ThorVG v1.0-pre34 built and installed successfully"
                    echo "   - Text API: ✓ Available"
                    echo "   - GL symbols: ✓ None (no conflicts)"
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

        cd - > /dev/null
        return 0
    else
        echo "Error: ThorVG source directory not found"
        return 1
    fi
}

# Build libcurl with minimal static build for Windows
echo "Setting up minimal static libcurl..."
if [ -f "$DEPS_DIR/lib/libcurl.a" ]; then
    echo "Minimal static libcurl already available"
else
    if ! build_minimal_static_libcurl; then
        echo "Warning: minimal libcurl build failed"
        exit 1
    else
        echo "Minimal static libcurl built successfully"
    fi
fi

# Build ThorVG for vector graphics rendering
echo ""
echo "Setting up ThorVG (vector graphics library)..."
if [ -f "$DEPS_DIR/lib/libthorvg.a" ]; then
    echo "ThorVG already available"
else
    if ! build_thorvg; then
        echo "⚠️  Warning: ThorVG build failed"
        echo "   ThorVG is required for SVG rendering in Radiant engine"
        echo "   You can manually build it later from: https://github.com/thorvg/thorvg"
    else
        echo "✅ ThorVG built successfully"
    fi
fi

# Verify compiler setup
echo ""
echo "Verifying compiler setup..."

if [[ "$MSYSTEM" == "CLANG64" ]]; then
    COMPILER_VERSION=$(clang --version 2>/dev/null | head -1 || echo "Clang not found")
    echo "Clang version: $COMPILER_VERSION"

    # Check if clang++ is available
    if command_exists clang++; then
        echo "✅ clang++ is available"
    else
        echo "⚠️  clang++ not found"
    fi
else
    GCC_VERSION=$(gcc --version 2>/dev/null | head -1 || echo "GCC not found")
    echo "GCC version: $GCC_VERSION"

    # Check if g++ is available
    if command_exists g++; then
        echo "✅ g++ is available"
    else
        echo "⚠️  g++ not found"
    fi
fi

# Check other tools
echo ""
echo "Verifying build tools..."
tools=("make" "cmake" "ninja" "git" "pkg-config" "premake5" "meson")
for tool in "${tools[@]}"; do
    if command_exists "$tool"; then
        version=$($tool --version 2>/dev/null | head -1 || echo "version unknown")
        echo "✅ $tool: $version"
    else
        echo "⚠️  $tool: not found"
    fi
done

# Configure ccache for optimal performance
echo ""
echo "Configuring ccache..."
if command_exists ccache; then
    ccache_version=$(ccache --version 2>/dev/null | head -1 || echo "version unknown")
    echo "✅ ccache: $ccache_version"

    # Set up ccache configuration
    ccache --set-config=max_size=5G
    ccache --set-config=compression=true
    ccache --set-config=compression_level=6
    ccache --set-config=stats=true

    echo "✅ ccache configured (5GB cache, compression enabled)"

    # Add ccache environment variables to ~/.bashrc if not already present
    if ! grep -q "export CC=\"ccache gcc\"" ~/.bashrc 2>/dev/null; then
        echo "" >> ~/.bashrc
        echo "# ccache configuration for faster compilation" >> ~/.bashrc
        echo "export MAKEFLAGS=\"-j\$(nproc)\"" >> ~/.bashrc
        echo "export CC=\"ccache gcc\"" >> ~/.bashrc
        echo "export CXX=\"ccache g++\"" >> ~/.bashrc
        echo "export CCACHE_DIR=\"\$HOME/.ccache\"" >> ~/.bashrc
        echo "export CCACHE_MAXSIZE=\"5G\"" >> ~/.bashrc
        echo "export CCACHE_COMPRESS=\"true\"" >> ~/.bashrc
        echo "✅ Added ccache configuration to ~/.bashrc"
        echo "   Note: Source your ~/.bashrc or restart your terminal for changes to take effect"
    else
        echo "✅ ccache already configured in ~/.bashrc"
    fi

    # Add /mingw64/bin to PATH if not already present (needed for runtime DLLs)
    if ! grep -q "export PATH=\"/mingw64/bin:\$PATH\"" ~/.bashrc 2>/dev/null; then
        echo "" >> ~/.bashrc
        echo "# Add MINGW64 binaries to PATH for lambda.exe runtime DLLs" >> ~/.bashrc
        echo "export PATH=\"/mingw64/bin:\$PATH\"" >> ~/.bashrc
        echo "✅ Added /mingw64/bin to PATH in ~/.bashrc"
    else
        echo "✅ /mingw64/bin already in PATH"
    fi
else
    echo "⚠️  ccache: not found"
fi

# Create build configuration for native Windows
echo ""
echo "Creating build configuration for native Windows..."

# Determine library paths based on MSYS2 environment
if [[ "$MSYSTEM" == "MINGW64" ]]; then
    MSYS2_PREFIX="/mingw64"
    MSYS2_LIB_PREFIX="mingw-w64-x86_64"
else
    MSYS2_PREFIX="/usr"
    MSYS2_LIB_PREFIX="unknown"
fi

echo ""
echo "🎉 Windows native compilation setup completed!"
echo ""
echo "Summary:"
echo "  • Dependencies directory: $DEPS_DIR"
echo "  • Build system: Premake5 (generates Makefiles from build_lambda_config.json)"
echo "  • Target environment: $MSYSTEM ($COMPILER_NAME)"
echo "  • Compiler cache: ccache enabled for faster rebuilds (5GB cache)"
echo ""
echo "To build Lambda for Windows natively:"
echo "  1. Ensure you're in MSYS2 terminal (CLANG64 or MINGW64)"
echo "  2. Restart terminal or run: source ~/.bashrc"
echo "  3. Run: make build"
echo ""
echo "For debug build:"
echo "  make build-debug"
echo ""
echo "For release build:"
echo "  make build-release"
echo ""
echo "Dependencies installed:"
echo "  ✅ Build tools (make, cmake, ninja, premake5, meson)"
echo "  ✅ Compiler toolchain ($COMPILER_NAME)"
echo "  ✅ ccache (compiler cache for 5-30x faster rebuilds)"
echo "  ✅ mpdecimal (multi-precision decimal arithmetic)"
echo "  ✅ mbedTLS (SSL/TLS library)"
echo "  ✅ FreeType (font rendering)"
echo "  ✅ GLFW (window management)"
echo "  ✅ Image libraries (PNG, JPEG, GIF)"
echo "  📦 Tree-sitter library (building from source - see verification below)"
echo "  📦 Tree-sitter-lambda (building from source - see verification below)"
echo "  📦 Tree-sitter-javascript (building from source - see verification below)"
echo "  📦 Tree-sitter-latex (building from source - see verification below)"
echo "  📦 MIR (building from source - see verification below)"
echo "  📦 utf8proc (building from source - see verification below)"
echo "  📦 libcurl minimal static (building from source - HTTP/HTTPS only, no HTTP/2)"
echo "  📦 ThorVG (building from source - vector graphics for SVG rendering)"
echo ""

# Final verification
echo "Performing final verification..."

# Check tree-sitter libraries
if [ -f "lambda/tree-sitter/libtree-sitter.a" ]; then
    echo "✅ Tree-sitter library available"
else
    echo "⚠️  Tree-sitter library missing"
fi

if [ -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
    echo "✅ Tree-sitter-lambda library available"
else
    echo "⚠️  Tree-sitter-lambda library missing"
fi

if [ -f "lambda/tree-sitter-javascript/libtree-sitter-javascript.a" ]; then
    echo "✅ Tree-sitter-javascript library available"
else
    echo "⚠️  Tree-sitter-javascript library missing"
fi

if [ -f "lambda/tree-sitter-latex/libtree-sitter-latex.a" ]; then
    echo "✅ Tree-sitter-latex library available"
else
    echo "⚠️  Tree-sitter-latex library missing"
fi

# Check MIR
if [ -f "$DEPS_DIR/lib/libmir.a" ]; then
    echo "✅ MIR library available"
else
    echo "⚠️  MIR library missing"
fi

# Check utf8proc
if [ -f "$DEPS_DIR/lib/libutf8proc.a" ]; then
    echo "✅ utf8proc library available"
else
    echo "⚠️  utf8proc library missing"
fi

# Check libcurl
if [ -f "$DEPS_DIR/lib/libcurl.a" ]; then
    echo "✅ libcurl library available (minimal static build)"
else
    echo "⚠️  libcurl library missing"
fi

# Check ThorVG
if [ -f "$DEPS_DIR/lib/libthorvg.a" ]; then
    echo "✅ ThorVG library available"
else
    echo "⚠️  ThorVG library missing (required for SVG rendering)"
fi

# Check mpdec (from MSYS2 package)
if pacman -Qq "${TOOLCHAIN_PREFIX}-mpdecimal" &>/dev/null; then
    echo "✅ mpdecimal library installed (from MSYS2)"
else
    echo "⚠️  mpdecimal library not installed"
fi

# Check mbedTLS (from MSYS2 package)
if pacman -Qq "${TOOLCHAIN_PREFIX}-mbedtls" &>/dev/null; then
    echo "✅ mbedTLS library installed (from MSYS2)"
else
    echo "⚠️  mbedTLS library not installed"
fi

# Check FreeType (from MSYS2 package)
if pacman -Qq "${TOOLCHAIN_PREFIX}-freetype" &>/dev/null; then
    echo "✅ FreeType library installed (from MSYS2)"
else
    echo "⚠️  FreeType library not installed"
fi

# Check GLFW (from MSYS2 package)
if pacman -Qq "${TOOLCHAIN_PREFIX}-glfw" &>/dev/null; then
    echo "✅ GLFW library installed (from MSYS2)"
else
    echo "⚠️  GLFW library not installed"
fi

# Check image libraries
if pacman -Qq "${TOOLCHAIN_PREFIX}-libpng" &>/dev/null; then
    echo "✅ libpng library installed (from MSYS2)"
else
    echo "⚠️  libpng library not installed"
fi

if pacman -Qq "${TOOLCHAIN_PREFIX}-libjpeg-turbo" &>/dev/null; then
    echo "✅ libjpeg-turbo library installed (from MSYS2)"
else
    echo "⚠️  libjpeg-turbo library not installed"
fi

if pacman -Qq "${TOOLCHAIN_PREFIX}-giflib" &>/dev/null; then
    echo "✅ giflib library installed (from MSYS2)"
else
    echo "⚠️  giflib library not installed"
fi

# Check compilers
if command_exists "$CC"; then
    echo "✅ C compiler ($CC) ready"
else
    echo "⚠️  C compiler not found"
fi

if command_exists "$CXX"; then
    echo "✅ C++ compiler ($CXX) ready"
else
    echo "⚠️  C++ compiler not found"
fi

echo ""
echo "Setup completed! You can now build Lambda natively for Windows."
echo ""
echo "🔨 To build Lambda:"
echo "  make build        # For native Windows build"
echo "  make build-windows # For cross-compilation target"
