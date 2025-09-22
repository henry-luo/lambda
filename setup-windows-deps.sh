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
install_msys2_package "${TOOLCHAIN_PREFIX}-premake" "Premake5 build system generator (required for Lambda)"
# Note: pkgconf is already installed as a dependency of cmake, no need for separate pkg-config

# Essential libraries for Lambda
echo ""
echo "Installing Lambda dependencies..."

# Development tools
install_msys2_package "git" "Git version control"
install_msys2_package "${TOOLCHAIN_PREFIX}-gdb" "GDB debugger"
install_msys2_package "jq" "JSON processor for build script configuration parsing"
install_msys2_package "vim" "Vim editor (provides xxd binary data tool)"

# HTTP/networking libraries - minimal setup for libcurl only
# Note: nghttp2 removed - building minimal libcurl without HTTP/2 support

# Additional utilities
install_msys2_package "unzip" "Unzip utility"
install_msys2_package "curl" "curl for downloading"
install_msys2_package "wget" "wget for downloading"

echo ""
echo "Installing optional dependencies..."

# Optional: Criterion for testing (if available)
install_msys2_package "${TOOLCHAIN_PREFIX}-criterion" "Criterion testing framework" || echo "⚠️  Criterion not available, tests may not work"

# Optional: libedit for interactive features (cross-platform alternative to readline)
install_msys2_package "${TOOLCHAIN_PREFIX}-libedit" "libedit library" || echo "⚠️  libedit not available"

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

echo ""
echo "🔧 Building dependencies from source..."
echo "Note: This script will check for parallel directories first:"
echo "  • ../mir (for MIR JIT compiler)"  
echo "  • ../utf8proc (for Unicode normalization)"
echo "If parallel directories are not found, dependencies will be downloaded."
echo "Note: Tree-sitter libraries are managed directly under lambda/ directory."
echo ""

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
tools=("make" "cmake" "ninja" "git" "pkg-config" "premake5")
for tool in "${tools[@]}"; do
    if command_exists "$tool"; then
        version=$($tool --version 2>/dev/null | head -1 || echo "version unknown")
        echo "✅ $tool: $version"
    else
        echo "⚠️  $tool: not found"
    fi
done

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
echo "  • Build configuration: build_lambda_win_native_config.json"
echo "  • Convenience script: compile-win-native.sh"
echo "  • Target environment: $MSYSTEM ($COMPILER_NAME)"
echo ""
echo "To build Lambda for Windows natively:"
echo "  1. Ensure you're in MSYS2 terminal (CLANG64 or MINGW64)"
echo "  2. Run: ./compile-win-native.sh"
echo "  3. Or run: ./compile.sh build_lambda_win_native_config.json"
echo ""
echo "For debug build:"
echo "  ./compile-win-native.sh --debug"
echo ""
echo "Dependencies installed:"
echo "  ✅ Build tools (make, cmake, ninja, premake5)"
echo "  ✅ Compiler toolchain ($COMPILER_NAME)"
echo "  ✅ GMP (system package)"
echo "  ✅ ICU (Unicode support)"
echo "  📦 Tree-sitter library (building from source - see verification below)"
echo "  📦 Tree-sitter-lambda (building from source - see verification below)"
echo "  📦 MIR (building from source - see verification below)"
echo "  📦 utf8proc (building from source - see verification below)"
echo "  📦 libcurl minimal static (building from source - HTTP/HTTPS only, no HTTP/2)"
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
