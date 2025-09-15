#!/bin/bash

# Glibc-based Linux cross-compilation setup for macOS
# Uses x86_64-unknown-linux-gnu toolchain with complete glibc runtime
set -e

SCRIPT_DIR="$(pwd)"
DEPS_DIR="linux-deps"
TARGET="x86_64-unknown-linux-gnu"
GLIBC_TOOLCHAIN_PATH="/opt/homebrew/Cellar/x86_64-unknown-linux-gnu/13.3.0"

# Library versions
MPDECIMAL_VERSION="4.0.1"
UTF8PROC_VERSION="2.10.0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check if we're on macOS
check_macos() {
    if [[ "$(uname)" != "Darwin" ]]; then
        print_error "This script is designed for macOS. Current OS: $(uname)"
        exit 1
    fi
    print_info "Running on macOS $(sw_vers -productVersion)"
}

# Function to check and install Homebrew
check_homebrew() {
    if ! command_exists brew; then
        print_error "Homebrew is required but not installed."
        print_info "Please install Homebrew first: https://brew.sh"
        exit 1
    fi
    print_success "Homebrew is available"
}

# Function to install Homebrew packages
install_brew_package() {
    local package="$1"
    local description="$2"
    
    if brew list "$package" &>/dev/null; then
        print_success "$description ($package) already installed"
        return 0
    fi
    
    print_info "Installing $description ($package)..."
    if brew install "$package"; then
        print_success "$description installed successfully"
        return 0
    else
        print_error "Failed to install $package"
        return 1
    fi
}

# Function to create directory structure
create_directories() {
    print_info "Creating directory structure..."
    mkdir -p "$DEPS_DIR"/{bin,lib,include,src,sysroot}
    mkdir -p "$DEPS_DIR"/sysroot/{lib,include,usr/include}
    print_success "Directory structure created"
}

# Function to setup cross-compilation environment
setup_cross_environment() {
    print_info "Setting up cross-compilation environment..."
    
    # Check if glibc toolchain is available
    if [[ ! -d "$GLIBC_TOOLCHAIN_PATH" ]]; then
        print_error "Glibc toolchain not found at $GLIBC_TOOLCHAIN_PATH"
        print_info "Please install it with:"
        print_info "  brew tap messense/macos-cross-toolchains"
        print_info "  brew install x86_64-unknown-linux-gnu"
        exit 1
    fi
    
    # Add glibc toolchain to PATH
    TOOLCHAIN_BIN="$GLIBC_TOOLCHAIN_PATH/bin"
    if [[ ":$PATH:" != *":$TOOLCHAIN_BIN:"* ]]; then
        export PATH="$TOOLCHAIN_BIN:$PATH"
        print_info "Added glibc toolchain to PATH: $TOOLCHAIN_BIN"
    fi
    
    print_success "Cross-compilation environment ready"
}

# Function to create cross-compilation wrapper scripts
create_wrappers() {
    print_info "Creating cross-compilation wrapper scripts..."
    
    # Use the glibc toolchain directly
    GCC_PATH="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc"
    GXX_PATH="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-g++"
    AR_PATH="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar"
    RANLIB_PATH="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ranlib"
    STRIP_PATH="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-strip"
    
    # Verify tools exist
    for tool in "$GCC_PATH" "$GXX_PATH" "$AR_PATH" "$RANLIB_PATH" "$STRIP_PATH"; do
        if [[ ! -f "$tool" ]]; then
            print_error "Cross-compilation tool not found: $tool"
            exit 1
        fi
    done
    
    # Create gcc wrapper for Linux
    cat > "$DEPS_DIR/bin/x86_64-linux-gnu-gcc" << EOF
#!/bin/bash
exec ${GCC_PATH} \\
    -static \\
    "\$@"
EOF

    # Create g++ wrapper for Linux  
    cat > "$DEPS_DIR/bin/x86_64-linux-gnu-g++" << EOF
#!/bin/bash
exec ${GXX_PATH} \\
    -static \\
    "\$@"
EOF

    # Create ar wrapper
    cat > "$DEPS_DIR/bin/x86_64-linux-gnu-ar" << EOF
#!/bin/bash
exec ${AR_PATH} "\$@"
EOF

    # Create ranlib wrapper
    cat > "$DEPS_DIR/bin/x86_64-linux-gnu-ranlib" << EOF
#!/bin/bash
exec ${RANLIB_PATH} "\$@"
EOF

    # Create strip wrapper
    cat > "$DEPS_DIR/bin/x86_64-linux-gnu-strip" << EOF
#!/bin/bash
exec ${STRIP_PATH} "\$@"
EOF

    # Make all wrappers executable
    chmod +x "$DEPS_DIR/bin"/*
    
    print_success "Cross-compilation wrappers created"
}

# Function to create basic runtime
create_basic_runtime() {
    print_info "Skipping minimal runtime creation..."
    print_info "Using clang's built-in capabilities for cross-compilation"
    print_success "Runtime setup complete"
}

# Function to test the cross-compilation setup
test_setup() {
    print_info "Testing cross-compilation setup..."
    
    # Create a simple test program
    cat > "$DEPS_DIR/test.c" << 'EOF'
#include <stdio.h>

int main() {
    printf("Hello from Linux cross-compiled binary!\n");
    return 0;
}
EOF

    # Try to compile it
    if "$DEPS_DIR/bin/${TARGET}-clang" -o "$DEPS_DIR/test-linux" "$DEPS_DIR/test.c"; then
        print_success "Test compilation successful"
        
        # Check if the binary was created and is for Linux
        if [[ -f "$DEPS_DIR/test-linux" ]]; then
            FILE_INFO=$(file "$DEPS_DIR/test-linux")
            if echo "$FILE_INFO" | grep -q "x86-64"; then
                print_success "Cross-compiled binary verified: $FILE_INFO"
            else
                print_warning "Binary created but may not be correct: $FILE_INFO"
            fi
        fi
        
        # Cleanup test files
        rm -f "$DEPS_DIR/test.c" "$DEPS_DIR/test-linux"
    else
        print_error "Test compilation failed"
        return 1
    fi
}

# Function to create environment setup script
create_env_script() {
    print_info "Creating environment setup script..."
    
    cat > "$DEPS_DIR/linux-cross-env.sh" << 'EOF'
#!/bin/bash
# Source this script to set up Linux cross-compilation environment

DEPS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Add cross-compilation tools to PATH
export PATH="$DEPS_DIR/bin:$PATH"

# Set cross-compilation environment variables
export CC="x86_64-unknown-linux-musl-clang"
export CXX="x86_64-unknown-linux-musl-clang++"
export AR="x86_64-unknown-linux-musl-ar"
export RANLIB="x86_64-unknown-linux-musl-ranlib"
export STRIP="x86_64-unknown-linux-musl-strip"

# Set target
export CROSS_COMPILE_TARGET="x86_64-unknown-linux-musl"

# Add LLVM tools to PATH
if [[ -d "/opt/homebrew/opt/llvm/bin" ]]; then
    export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
fi

echo "Linux cross-compilation environment loaded"
echo "Target: $CROSS_COMPILE_TARGET"
echo "CC: $CC"
echo "CXX: $CXX"
EOF

    chmod +x "$DEPS_DIR/linux-cross-env.sh"
    print_success "Environment setup script created: $DEPS_DIR/linux-cross-env.sh"
}

# Function to build mpdecimal library for Linux
build_mpdecimal() {
    print_info "Building mpdecimal library for Linux..."
    
    cd "$DEPS_DIR/src"
    
    # Download mpdecimal if not exists
    if [[ ! -f "mpdecimal-${MPDECIMAL_VERSION}.tar.gz" ]]; then
        print_info "Downloading mpdecimal ${MPDECIMAL_VERSION}..."
        curl -L "https://www.bytereef.org/software/mpdecimal/releases/mpdecimal-${MPDECIMAL_VERSION}.tar.gz" -o "mpdecimal-${MPDECIMAL_VERSION}.tar.gz"
    fi
    
    # Extract if directory doesn't exist
    if [[ ! -d "mpdecimal-${MPDECIMAL_VERSION}" ]]; then
        print_info "Extracting mpdecimal..."
        tar -xzf "mpdecimal-${MPDECIMAL_VERSION}.tar.gz"
    fi
    
    cd "mpdecimal-${MPDECIMAL_VERSION}"
    
    if [[ ! -f "../mpdecimal-built.marker" ]]; then
        print_info "Configuring mpdecimal for ${TARGET}..."
        
        # Use the glibc toolchain
        export CC="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc"
        export CXX="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-g++"
        export AR="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar"
        export RANLIB="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ranlib"
        export CFLAGS="-static -O2"
        export CXXFLAGS="-static -O2"
        export LDFLAGS="-static"
        
        # Configure with cross-compilation settings
        ./configure \
            --host=x86_64-linux-gnu \
            --prefix="$SCRIPT_DIR/$DEPS_DIR" \
            --disable-shared \
            --enable-static \
            --disable-doc
        
        print_info "Building mpdecimal..."
        make -j$(sysctl -n hw.ncpu)
        
        print_info "Installing mpdecimal..."
        make install
        
        touch "../mpdecimal-built.marker"
        print_success "mpdecimal built and installed"
    else
        print_success "mpdecimal already built"
    fi
    
    cd "$SCRIPT_DIR"
}

# Function to build tree-sitter library for Linux
build_tree_sitter() {
    print_info "Building tree-sitter library for Linux..."
    
    # Copy tree-sitter to linux-deps for cross-compilation
    if [[ ! -d "$DEPS_DIR/tree-sitter" ]]; then
        print_info "Copying tree-sitter source to linux-deps..."
        cp -r lambda/tree-sitter "$DEPS_DIR/"
    fi
    
    cd "$DEPS_DIR/tree-sitter"
    
    if [[ ! -f "../tree-sitter-built.marker" ]]; then
        print_info "Building tree-sitter for ${TARGET}..."
        
        # Clean any previous build
        make clean >/dev/null 2>&1 || true
        
        # Use the glibc toolchain for cross-compilation
        export CC="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc"
        export AR="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar"
        export RANLIB="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ranlib"
        export STRIP="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-strip"
        export CFLAGS="-static -O2"
        export LDFLAGS="-static"
        
        # Build only the static library (avoid shared library issues)
        print_info "Compiling tree-sitter static library..."
        make -j$(sysctl -n hw.ncpu) libtree-sitter.a || {
            print_error "Failed to build tree-sitter"
            exit 1
        }
        
        # Install headers and library
        print_info "Installing tree-sitter..."
        mkdir -p "$SCRIPT_DIR/$DEPS_DIR/include/tree_sitter"
        mkdir -p "$SCRIPT_DIR/$DEPS_DIR/lib"
        
        # Copy headers
        cp lib/include/tree_sitter/api.h "$SCRIPT_DIR/$DEPS_DIR/include/tree_sitter/" || {
            print_error "Failed to install tree-sitter headers"
            exit 1
        }
        
        # Copy library
        cp libtree-sitter.a "$SCRIPT_DIR/$DEPS_DIR/lib/" || {
            print_error "Failed to install tree-sitter library"
            exit 1
        }
        
        touch "../tree-sitter-built.marker"
        print_success "tree-sitter built and installed"
    else
        print_success "tree-sitter already built"
    fi
    
    cd "$SCRIPT_DIR"
}

# Function to build MIR for Linux
build_mir() {
    print_info "Building MIR for Linux..."
    
    cd "$DEPS_DIR/src"
    
    # Clone MIR if not exists
    if [[ ! -d "mir" ]]; then
        print_info "Cloning MIR repository..."
        git clone https://github.com/vnmakarov/mir.git || {
            print_error "Could not clone MIR repository"
            return 1
        }
    fi
    
    cd mir
    
    # Check if already built
    if [[ -f "../../../lib/libmir.a" ]]; then
        print_success "MIR already built"
        cd "$SCRIPT_DIR"
        return 0
    fi
    
    # Clean previous builds
    make clean 2>/dev/null || true
    
    # Set cross-compilation environment
    export CC="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-gcc"
    export CXX="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-g++"
    export AR="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-ar"
    export RANLIB="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-ranlib"
    export STRIP="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-strip"
    
    # MIR-specific cross-compilation flags
    export CFLAGS="-O2 -DNDEBUG -static -fPIC"
    export CXXFLAGS="-O2 -DNDEBUG -static -fPIC"
    export LDFLAGS="-static"
    
    print_info "Building MIR with cross-compiler..."
    
    # Build MIR using GNUmakefile (static library only)
    if [[ -f "GNUmakefile" ]]; then
        # Use the GNUmakefile approach - build only static library
        make -f GNUmakefile CC="$CC" AR="$AR" CFLAGS="$CFLAGS" libmir.a -j$(nproc 2>/dev/null || echo 4) || {
            print_error "Failed to build MIR"
            return 1
        }
        
        # Copy built libraries and headers
        if [[ -f "libmir.a" ]]; then
            cp libmir.a "../../../lib/"
            print_success "MIR library built: $(ls -lah ../../../lib/libmir.a | awk '{print $5}')"
        else
            print_error "MIR library not found after build"
            return 1
        fi
        
        # Copy headers
        mkdir -p "../../../include"
        for header in mir.h mir-gen.h mir-varr.h mir-dlist.h mir-hash.h mir-htab.h mir-alloc.h mir-bitmap.h mir-code-alloc.h; do
            if [[ -f "$header" ]]; then
                cp "$header" "../../../include/"
                print_info "Copied header: $header"
            fi
        done
        
        # Copy c2mir header if available
        if [[ -f "c2mir/c2mir.h" ]]; then
            cp "c2mir/c2mir.h" "../../../include/"
            print_info "Copied header: c2mir/c2mir.h"
        fi
    else
        print_error "MIR GNUmakefile not found"
        return 1
    fi
    
    cd "$SCRIPT_DIR"
}

# Function to build curl for Linux
build_curl() {
    print_info "Building curl for Linux..."
    
    cd "$DEPS_DIR/src"
    
    # Download curl if not exists
    if [[ ! -f "curl-8.10.1.tar.gz" ]]; then
        print_info "Downloading curl 8.10.1..."
        curl -L "https://curl.se/download/curl-8.10.1.tar.gz" -o "curl-8.10.1.tar.gz" || {
            print_error "Failed to download curl"
            return 1
        }
    fi
    
    # Extract if directory doesn't exist
    if [[ ! -d "curl-8.10.1" ]]; then
        print_info "Extracting curl..."
        tar -xzf "curl-8.10.1.tar.gz"
    fi
    
    cd curl-8.10.1
    
    # Check if already built
    if [[ -f "../../../lib/libcurl.a" ]]; then
        print_success "curl already built"
        cd "$SCRIPT_DIR"
        return 0
    fi
    
    # Clean previous builds
    make clean 2>/dev/null || true
    
    # Set cross-compilation environment
    export CC="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-gcc"
    export CXX="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-g++"
    export AR="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-ar"
    export RANLIB="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-ranlib"
    export STRIP="${GLIBC_TOOLCHAIN_PATH}/bin/x86_64-linux-gnu-strip"
    
    # Configure for cross-compilation
    print_info "Configuring curl for Linux cross-compilation..."
    ./configure \
        --host=x86_64-linux-gnu \
        --enable-static \
        --disable-shared \
        --with-openssl=no \
        --with-mbedtls=no \
        --with-gnutls=no \
        --without-ssl \
        --disable-ftp \
        --disable-ldap \
        --disable-ldaps \
        --disable-rtsp \
        --disable-proxy \
        --disable-dict \
        --disable-telnet \
        --disable-tftp \
        --disable-pop3 \
        --disable-imap \
        --disable-smb \
        --disable-smtp \
        --disable-gopher \
        --disable-mqtt \
        --disable-manual \
        --disable-ipv6 \
        --disable-versioned-symbols \
        --prefix="$(pwd)/../../../" \
        CFLAGS="-O2 -DNDEBUG -static" \
        LDFLAGS="-static" || {
        print_error "Failed to configure curl"
        return 1
    }
    
    # Build curl
    print_info "Building curl..."
    make -j$(nproc 2>/dev/null || echo 4) || {
        print_error "Failed to build curl"
        return 1
    }
    
    # Install to linux-deps
    print_info "Installing curl libraries and headers..."
    make install || {
        print_error "Failed to install curl"
        return 1
    }
    
    # Verify library was built
    if [[ -f "../../../lib/libcurl.a" ]]; then
        print_success "curl library built: $(ls -lah ../../../lib/libcurl.a | awk '{print $5}')"
    else
        print_error "curl library not found after build"
        return 1
    fi
    
    cd "$SCRIPT_DIR"
}

# Function to build utf8proc library for Linux  
build_utf8proc() {
    print_info "Building utf8proc library for Linux..."
    
    cd "$DEPS_DIR/src"
    
    # Download utf8proc if not exists
    if [[ ! -f "utf8proc-${UTF8PROC_VERSION}.tar.gz" ]]; then
        print_info "Downloading utf8proc ${UTF8PROC_VERSION}..."
        curl -L "https://github.com/JuliaStrings/utf8proc/archive/v${UTF8PROC_VERSION}.tar.gz" -o "utf8proc-${UTF8PROC_VERSION}.tar.gz"
    fi
    
    # Extract if directory doesn't exist
    if [[ ! -d "utf8proc-${UTF8PROC_VERSION}" ]]; then
        print_info "Extracting utf8proc..."
        tar -xzf "utf8proc-${UTF8PROC_VERSION}.tar.gz"
    fi
    
    cd "utf8proc-${UTF8PROC_VERSION}"
    
    if [[ ! -f "../utf8proc-built.marker" ]]; then
        print_info "Building utf8proc for ${TARGET}..."
        
        # Use the glibc toolchain
        export CC="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc"
        export AR="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar"
        export RANLIB="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ranlib"
        export CFLAGS="-static -O2"
        export LDFLAGS="-static"
        
        # Build static library only
        make -j$(sysctl -n hw.ncpu) libutf8proc.a PREFIX="$SCRIPT_DIR/$DEPS_DIR"
        
        print_info "Installing utf8proc..."
        # Manual installation to avoid building dynamic libraries
        mkdir -p "$SCRIPT_DIR/$DEPS_DIR/include"
        mkdir -p "$SCRIPT_DIR/$DEPS_DIR/lib"
        cp utf8proc.h "$SCRIPT_DIR/$DEPS_DIR/include/"
        cp libutf8proc.a "$SCRIPT_DIR/$DEPS_DIR/lib/"
        
        touch "../utf8proc-built.marker"
        print_success "utf8proc built and installed"
    else
        print_success "utf8proc already built"
    fi
    
    cd "$SCRIPT_DIR"
}

# Main function
main() {
    print_info "Setting up simplified Linux cross-compilation for Lambda Script..."
    print_info "Target: $TARGET"
    print_info "Dependencies directory: $DEPS_DIR"
    echo
    
    check_macos
    check_homebrew
    
    create_directories
    setup_cross_environment
    create_wrappers
    create_basic_runtime
    
    # Build required libraries
    build_tree_sitter
    build_mir
    build_curl
    build_mpdecimal
    build_utf8proc
    
    create_env_script
    
    print_info "Testing setup..."
    if test_setup; then
        echo
        print_success "Linux cross-compilation setup completed successfully!"
        echo
        print_info "To use the cross-compilation environment:"
        print_info "  source $DEPS_DIR/linux-cross-env.sh"
        print_info "  make build-linux"
        echo
        print_info "Libraries built:"
        print_info "  - mpdecimal ${MPDECIMAL_VERSION} (static)"
        print_info "  - utf8proc ${UTF8PROC_VERSION} (static)"
        echo
    else
        print_warning "Setup completed but test had issues - proceeding anyway"
        print_info "You may need to adjust compiler flags for your specific use case"
    fi
}

# Run main function
main "$@"