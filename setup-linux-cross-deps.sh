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
LIBEDIT_VERSION="20240808-3.1"
CATCH2_VERSION="3.10.0"

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
    if "$DEPS_DIR/bin/x86_64-linux-gnu-gcc" -o "$DEPS_DIR/test-linux" "$DEPS_DIR/test.c"; then
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
export CC="x86_64-linux-gnu-gcc"
export CXX="x86_64-linux-gnu-g++"
export AR="x86_64-linux-gnu-ar"
export RANLIB="x86_64-linux-gnu-ranlib"
export STRIP="x86_64-linux-gnu-strip"

# Set target
export CROSS_COMPILE_TARGET="x86_64-unknown-linux-gnu"

# Add glibc toolchain to PATH
if [[ -d "/opt/homebrew/Cellar/x86_64-unknown-linux-gnu/13.3.0/bin" ]]; then
    export PATH="/opt/homebrew/Cellar/x86_64-unknown-linux-gnu/13.3.0/bin:$PATH"
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
        --without-libpsl \
        --without-zlib \
        --without-brotli \
        --without-zstd \
        --without-librtmp \
        --without-libssh2 \
        --without-libssh \
        --without-libgsasl \
        --without-winidn \
        --without-libidn2 \
        --without-nghttp2 \
        --without-ngtcp2 \
        --without-nghttp3 \
        --without-quiche \
        --without-msh3 \
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
        --disable-cookies \
        --disable-crypto-auth \
        --disable-tls-srp \
        --disable-unix-sockets \
        --disable-doh \
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

# Function to build minimal ncurses stub for Linux
build_ncurses() {
    print_info "Creating minimal ncurses stub for Linux..."
    
    # Check if already built
    if [[ -f "$SCRIPT_DIR/$DEPS_DIR/lib/libncurses.a" ]]; then
        print_success "ncurses stub already built"
        return 0
    fi
    
    # Create minimal ncurses stub implementation
    mkdir -p "$SCRIPT_DIR/$DEPS_DIR/include/ncurses"
    mkdir -p "$SCRIPT_DIR/$DEPS_DIR/lib"
    
    # Create minimal curses.h header
    cat > "$SCRIPT_DIR/$DEPS_DIR/include/curses.h" << 'EOF'
#ifndef _CURSES_H
#define _CURSES_H

// Minimal curses.h stub for cross-compilation
typedef struct _win_st WINDOW;
typedef unsigned long chtype;

#define OK (0)
#define ERR (-1)
#define TRUE 1
#define FALSE 0

// Terminal capability functions (stubs)
extern int setupterm(const char *term, int filedes, int *errret);
extern int tigetflag(const char *capname);
extern int tigetnum(const char *capname);
extern char *tigetstr(const char *capname);
extern char *tparm(const char *str, ...);
extern int putp(const char *str);
extern int tputs(const char *str, int affcnt, int (*putc)(int));

// Basic terminal functions
extern WINDOW *initscr(void);
extern int endwin(void);
extern int cbreak(void);
extern int nocbreak(void);
extern int echo(void);
extern int noecho(void);
extern int keypad(WINDOW *win, int bf);
extern int nodelay(WINDOW *win, int bf);

#endif /* _CURSES_H */
EOF

    # Create minimal ncurses stub library
    cat > "$SCRIPT_DIR/$DEPS_DIR/lib/ncurses_stub.c" << 'EOF'
// Minimal ncurses stub implementation for cross-compilation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _win_st WINDOW;
typedef unsigned long chtype;

// Terminal capability stubs
int setupterm(const char *term, int filedes, int *errret) {
    if (errret) *errret = 1;
    return -1;
}

int tigetflag(const char *capname) { return 0; }
int tigetnum(const char *capname) { return -1; }
char *tigetstr(const char *capname) { return (char*)-1; }

char *tparm(const char *str, ...) { return NULL; }
int putp(const char *str) { return -1; }
int tputs(const char *str, int affcnt, int (*putc)(int)) { return -1; }

// Terminal entry function (required by libedit)
int tgetent(char *bp, const char *name) { return 1; }
int tgetflag(const char *id) { return 0; }
int tgetnum(const char *id) { return -1; }
char *tgetstr(const char *id, char **area) { return NULL; }
char *tgoto(const char *cap, int col, int row) { return NULL; }

// Basic terminal function stubs
WINDOW *initscr(void) { return NULL; }
int endwin(void) { return 0; }
int cbreak(void) { return 0; }
int nocbreak(void) { return 0; }
int echo(void) { return 0; }
int noecho(void) { return 0; }
int keypad(WINDOW *win, int bf) { return 0; }
int nodelay(WINDOW *win, int bf) { return 0; }

// Fallback function for missing symbol
const char* _nc_fallback(const char* term) {
    return (const char*)0;
}
EOF

    # Compile the stub library
    print_info "Compiling ncurses stub..."
    "$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc" -c "$SCRIPT_DIR/$DEPS_DIR/lib/ncurses_stub.c" -o "$SCRIPT_DIR/$DEPS_DIR/lib/ncurses_stub.o"
    "$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar" rcs "$SCRIPT_DIR/$DEPS_DIR/lib/libncurses.a" "$SCRIPT_DIR/$DEPS_DIR/lib/ncurses_stub.o"
    "$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ranlib" "$SCRIPT_DIR/$DEPS_DIR/lib/libncurses.a"
    
    # Also create libncursesw.a (wide character version)
    cp "$SCRIPT_DIR/$DEPS_DIR/lib/libncurses.a" "$SCRIPT_DIR/$DEPS_DIR/lib/libncursesw.a"
    
    print_success "ncurses stub created successfully"
}

# Function to build criterion testing framework for Linux
build_criterion() {
    print_info "Building Criterion testing framework for Linux..."
    
    local VERSION="2.4.2"
    local URL="https://github.com/Snaipe/Criterion/archive/v${VERSION}.tar.gz"
    local LOG_FILE="$SCRIPT_DIR/criterion_build.log"
    
    # Clear previous log
    > "$LOG_FILE"
    
    cd /tmp
    rm -rf criterion-*
    
    print_info "Downloading Criterion ${VERSION}..."
    curl -LO "$URL" 2>&1 | tee -a "$LOG_FILE"
    
    print_info "Extracting Criterion..."
    tar -xzf "v${VERSION}.tar.gz" 2>&1 | tee -a "$LOG_FILE"
    cd "criterion-${VERSION}"
    
    print_info "Configuring Criterion for cross-compilation with Meson..."
    echo "=== Creating cross-compilation file ===" >> "$LOG_FILE"
    # Create cross-compilation file for Meson
    cat > cross-file.txt << EOF
[binaries]
c = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc'
cpp = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-g++'
ar = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar'
strip = '$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-strip'
pkg-config = '/usr/bin/false'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
    
    echo "=== Cross-compilation file contents ===" >> "$LOG_FILE"
    cat cross-file.txt >> "$LOG_FILE"
    echo "=== End cross-compilation file ===" >> "$LOG_FILE"
    
    echo "=== Running meson setup ===" >> "$LOG_FILE"
    meson setup build \
        --cross-file=cross-file.txt \
        --prefix="$SCRIPT_DIR/$DEPS_DIR" \
        --buildtype=release \
        -Dtests=false \
        -Dsamples=false 2>&1 | tee -a "$LOG_FILE"
    
    if [ $? -ne 0 ]; then
        print_error "Meson setup failed. Check $LOG_FILE for details."
        cd "$SCRIPT_DIR"
        return 1
    fi
    
    print_info "Building Criterion..."
    echo "=== Running ninja build ===" >> "$LOG_FILE"
    cd build
    ninja 2>&1 | tee -a "$LOG_FILE"
    
    if [ $? -ne 0 ]; then
        print_error "Ninja build failed. Check $LOG_FILE for details."
        cd "$SCRIPT_DIR"
        return 1
    fi
    
    print_info "Installing Criterion..."
    echo "=== Running ninja install ===" >> "$LOG_FILE"
    ninja install 2>&1 | tee -a "$LOG_FILE"
    
    if [ $? -ne 0 ]; then
        print_error "Ninja install failed. Check $LOG_FILE for details."
        cd "$SCRIPT_DIR"
        return 1
    fi
    
    print_success "Criterion build completed successfully! Log saved to $LOG_FILE"
    cd "$SCRIPT_DIR"
}

# Function to build libedit library for Linux
build_libedit() {
    print_info "Building libedit library for Linux..."
    
    local VERSION="20230828-3.1"
    local URL="https://www.thrysoee.dk/editline/libedit-${VERSION}.tar.gz"
    
    cd /tmp
    rm -rf libedit-*
    
    print_info "Downloading libedit ${VERSION}..."
    curl -LO "$URL"
    
    print_info "Extracting libedit..."
    tar -xzf "libedit-${VERSION}.tar.gz"
    cd "libedit-${VERSION}"
    
    print_info "Configuring libedit for cross-compilation..."
    CC="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc" \
    CXX="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-g++" \
    LDFLAGS="-L$SCRIPT_DIR/$DEPS_DIR/lib" \
    CPPFLAGS="-I$SCRIPT_DIR/$DEPS_DIR/include -I$SCRIPT_DIR/$DEPS_DIR/include/ncurses" \
    ./configure \
        --host=x86_64-linux-gnu \
        --prefix="$SCRIPT_DIR/$DEPS_DIR" \
        --enable-static \
        --disable-shared \
        --disable-examples \
        --with-pic 2>/dev/null
    
    print_info "Building libedit..."
    make -j$(nproc 2>/dev/null || echo 4) 2>/dev/null
    
    print_info "Installing libedit..."
    make install 2>/dev/null
    
    print_success "libedit build completed"
    cd "$SCRIPT_DIR"
}

# Function to build Catch2 testing framework for Linux
build_catch2() {
    print_info "Building Catch2 testing framework for Linux..."
    
    cd "$DEPS_DIR"
    
    # Download Catch2 if not already present
    if [[ ! -f "Catch2-${CATCH2_VERSION}.tar.gz" ]]; then
        print_info "Downloading Catch2 v${CATCH2_VERSION}..."
        curl -L -o "Catch2-${CATCH2_VERSION}.tar.gz" "https://github.com/catchorg/Catch2/archive/v${CATCH2_VERSION}.tar.gz"
    fi
    
    # Extract Catch2
    if [[ ! -d "Catch2-${CATCH2_VERSION}" ]]; then
        print_info "Extracting Catch2..."
        tar -xzf "Catch2-${CATCH2_VERSION}.tar.gz"
    fi
    
    cd "Catch2-${CATCH2_VERSION}"
    
    # Check if already built
    if [[ -f "../../lib/libCatch2Maind.a" ]] && [[ -f "../../lib/libCatch2d.a" ]]; then
        print_success "Catch2 already built"
        cd "$SCRIPT_DIR"
        return 0
    fi
    
    # Create build directory
    mkdir -p build-linux
    cd build-linux
    
    print_info "Configuring Catch2 for Linux cross-compilation..."
    
    # Set cross-compilation environment
    export CC="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-gcc"
    export CXX="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-g++"
    export AR="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ar"
    export RANLIB="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-ranlib"
    export STRIP="$GLIBC_TOOLCHAIN_PATH/bin/x86_64-linux-gnu-strip"
    
    # Configure with CMake for cross-compilation
    cmake .. \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
        -DCMAKE_C_COMPILER="${CC}" \
        -DCMAKE_CXX_COMPILER="${CXX}" \
        -DCMAKE_AR="${AR}" \
        -DCMAKE_RANLIB="${RANLIB}" \
        -DCMAKE_STRIP="${STRIP}" \
        -DCMAKE_INSTALL_PREFIX="$SCRIPT_DIR/$DEPS_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DBUILD_TESTING=OFF \
        -DCATCH_INSTALL_DOCS=OFF \
        -DCATCH_INSTALL_EXTRAS=OFF \
        -DCMAKE_CXX_FLAGS="-static" \
        -DCMAKE_C_FLAGS="-static"
    
    print_info "Building Catch2..."
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
    
    print_info "Installing Catch2..."
    make install
    
    # Create debug versions with 'd' suffix as expected by the test config
    cd "$SCRIPT_DIR/$DEPS_DIR"
    if [[ -f "lib/libCatch2Main.a" ]] && [[ -f "lib/libCatch2.a" ]]; then
        cp lib/libCatch2Main.a lib/libCatch2Maind.a
        cp lib/libCatch2.a lib/libCatch2d.a
        
        print_success "Catch2 installed successfully for Linux cross-compilation!"
        print_info "Libraries available:"
        ls -la lib/libCatch2*.a
    else
        print_error "Catch2 installation failed - libraries not found"
        cd "$SCRIPT_DIR"
        return 1
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
    build_ncurses
    build_libedit
    build_criterion
    build_catch2
    
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
        print_info "  - catch2 ${CATCH2_VERSION} (static)"
        echo
    else
        print_warning "Setup completed but test had issues - proceeding anyway"
        print_info "You may need to adjust compiler flags for your specific use case"
    fi
}

# Run main function
main "$@"