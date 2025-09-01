#!/bin/bash

# Linux (Ubuntu) native compilation dependency setup script
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
    
    echo "Cleanup completed."
    exit 0
fi

echo "Setting up Linux (Ubuntu) native compilation dependencies..."

# Check for required tools
check_tool() {
    local tool="$1"
    local package="$2"
    
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: $tool is required but not installed."
        echo "Install it with: sudo apt update && sudo apt install $package"
        return 1
    fi
    return 0
}

# Check if running on Ubuntu/Debian
if ! command -v apt >/dev/null 2>&1; then
    echo "Warning: This script is designed for Ubuntu/Debian systems with apt package manager."
    echo "You may need to adapt the package installation commands for your distribution."
fi

# Check for essential build tools
echo "Checking for essential build tools..."
check_tool "make" "build-essential" || exit 1
check_tool "gcc" "build-essential" || exit 1
check_tool "g++" "build-essential" || exit 1
check_tool "git" "git" || exit 1

# Check for cmake (needed for some dependencies)
if ! command -v cmake >/dev/null 2>&1; then
    echo "Installing cmake..."
    sudo apt update
    sudo apt install -y cmake
fi

# Check for pkg-config (often needed for library detection)
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Installing pkg-config..."
    sudo apt update
    sudo apt install -y pkg-config
fi

# Install additional development tools if not present
echo "Installing additional development dependencies..."
sudo apt update
sudo apt install -y \
    curl \
    wget \
    build-essential \
    cmake \
    git \
    pkg-config \
    libtool \
    autoconf \
    automake \
    python3 \
    python3-pip \
    libmpdec-dev \
    libedit-dev \
    libcurl4-openssl-dev \
    libutf8proc-dev \
    coreutils

# Try to install criterion via apt, build from source if not available
echo "Installing criterion testing framework..."
if dpkg -l | grep -q libcriterion-dev; then
    echo "libcriterion-dev already installed"
elif sudo apt install -y libcriterion-dev; then
    echo "✅ libcriterion-dev installed successfully via apt"
else
    echo "libcriterion-dev not available via apt, will build from source later"
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
    
    echo "Building $name for Linux..."
    
    if [ ! -d "build_temp/$src_dir" ]; then
        echo "Warning: Source directory build_temp/$src_dir not found"
        return 1
    fi
    
    cd "build_temp/$src_dir"
    
    # Set up environment for native Linux compilation
    export CC="gcc"
    export CXX="g++"
    export AR="ar"
    export STRIP="strip"
    export RANLIB="ranlib"
    export CFLAGS="-O2 -fPIC"
    export CXXFLAGS="-O2 -fPIC"
    export LDFLAGS=""
    
    # Add standard library paths
    export CPPFLAGS="-I/usr/include -I/usr/local/include $CPPFLAGS"
    export LDFLAGS="-L/usr/lib -L/usr/local/lib $LDFLAGS"
    export PKG_CONFIG_PATH="/usr/lib/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
    
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

# Function to build GMP for Linux
build_gmp_for_linux() {
    echo "Building GMP for Linux..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libgmp.a" ] || [ -f "$SYSTEM_PREFIX/lib/libgmp.so" ]; then
        echo "GMP already installed in system location"
        return 0
    fi
    
    # Try apt package first for easier installation
    echo "Attempting to install GMP via apt..."
    if dpkg -l | grep -q libgmp-dev; then
        echo "GMP development package already installed"
        return 0
    else
        echo "Installing GMP via apt..."
        if sudo apt update && sudo apt install -y libgmp-dev; then
            echo "✅ GMP installed successfully via apt"
            return 0
        else
            echo "apt installation failed, building from source..."
        fi
    fi
    
    # Build from source if apt is not available or failed
    GMP_VERSION="6.3.0"
    GMP_DIR="gmp-${GMP_VERSION}"
    GMP_ARCHIVE="${GMP_DIR}.tar.xz"
    GMP_URL="https://gmplib.org/download/gmp/${GMP_ARCHIVE}"
    
    download_extract "$GMP_DIR" "$GMP_URL" "$GMP_ARCHIVE"
    
    cd "build_temp/$GMP_DIR"
    
    echo "Configuring GMP..."
    if ./configure \
        --prefix="$SYSTEM_PREFIX" \
        --enable-static \
        --enable-shared \
        --enable-cxx; then
        
        echo "Building GMP..."
        if make -j$(nproc); then
            echo "Installing GMP to system location (requires sudo)..."
            sudo make install
            
            # Update library cache
            sudo ldconfig
            
            # Verify the build
            if [ -f "$SYSTEM_PREFIX/lib/libgmp.a" ]; then
                echo "✅ GMP library installed successfully"
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    
    echo "❌ GMP build failed"
    cd - > /dev/null
    return 1
}

# Function to build lexbor for Linux
build_lexbor_for_linux() {
    echo "Building lexbor for Linux..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/liblexbor_static.a" ] || [ -f "$SYSTEM_PREFIX/lib/liblexbor.a" ]; then
        echo "lexbor already installed in system location"
        return 0
    fi
    
    # Try apt package first (if available)
    echo "Checking for lexbor package..."
    if dpkg -l | grep -q liblexbor-dev; then
        echo "lexbor development package already installed"
        return 0
    else
        # lexbor is not commonly available in Ubuntu repos, so build from source
        echo "lexbor not available via apt, building from source..."
    fi
    
    # Build from source
    if [ ! -d "build_temp/lexbor" ]; then
        cd build_temp
        echo "Cloning lexbor repository..."
        git clone https://github.com/lexbor/lexbor.git || {
            echo "Warning: Could not clone lexbor repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "build_temp/lexbor"
    
    # Check if CMakeLists.txt exists
    if [ ! -f "CMakeLists.txt" ]; then
        echo "Warning: CMakeLists.txt not found in lexbor directory"
        cd - > /dev/null
        return 1
    fi
    
    # Create build directory
    mkdir -p build-linux
    cd build-linux
    
    echo "Configuring lexbor with CMake..."
    if cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DLEXBOR_BUILD_SHARED=ON \
        -DLEXBOR_BUILD_STATIC=ON \
        -DLEXBOR_BUILD_TESTS=OFF \
        -DLEXBOR_BUILD_EXAMPLES=OFF \
        -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" \
        ..; then
        
        echo "Building lexbor..."
        if make -j$(nproc); then
            echo "Installing lexbor to system location (requires sudo)..."
            sudo make install
            
            # Update library cache
            sudo ldconfig
            
            # Verify the build
            if [ -f "$SYSTEM_PREFIX/lib/liblexbor_static.a" ] || [ -f "$SYSTEM_PREFIX/lib/liblexbor.a" ]; then
                echo "✅ lexbor built successfully"
                cd - > /dev/null
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    
    echo "❌ lexbor build failed"
    cd - > /dev/null
    cd - > /dev/null
    return 1
}

# Function to build utf8proc for Linux
build_utf8proc_for_linux() {
    echo "Building utf8proc for Linux..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ]; then
        echo "utf8proc already installed in system location"
        return 0
    fi
    
    # Build from source
    if [ ! -d "build_temp/utf8proc" ]; then
        cd build_temp
        echo "Cloning utf8proc repository..."
        git clone https://github.com/JuliaStrings/utf8proc.git || {
            echo "Warning: Could not clone utf8proc repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "build_temp/utf8proc"
    
    # Check if Makefile exists
    if [ ! -f "Makefile" ]; then
        echo "Warning: Makefile not found in utf8proc directory"
        cd - > /dev/null
        return 1
    fi
    
    echo "Building utf8proc..."
    if make -j$(nproc); then
        echo "Installing utf8proc to system location (requires sudo)..."
        sudo make install
        
        # Update library cache
        sudo ldconfig
        
        # Verify the build
        if [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ]; then
            echo "✅ utf8proc built successfully"
            cd - > /dev/null
            return 0
        fi
    fi
    
    echo "❌ utf8proc build failed"
    cd - > /dev/null
    return 1
}

# Function to build MIR for Linux
build_mir_for_linux() {
    echo "Building MIR for Linux..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ] || [ -f "$SYSTEM_PREFIX/lib/libmir.so" ]; then
        echo "MIR already installed in system location"
        return 0
    fi
    
    # Create build_temp directory if it doesn't exist
    mkdir -p "build_temp"
    
    # Build from source
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
    
    # Check if GNUmakefile exists (MIR uses GNUmakefile, not Makefile)
    if [ ! -f "GNUmakefile" ]; then
        echo "Warning: GNUmakefile not found in MIR directory"
        cd - > /dev/null
        return 1
    fi
    
    echo "Building MIR..."
    if make -j$(nproc); then
        echo "Installing MIR to system location (requires sudo)..."
        # Create directories if they don't exist
        sudo mkdir -p "$SYSTEM_PREFIX/lib"
        sudo mkdir -p "$SYSTEM_PREFIX/include"
        
        # Copy library and headers
        sudo cp libmir.a "$SYSTEM_PREFIX/lib/" || sudo cp mir.a "$SYSTEM_PREFIX/lib/libmir.a"
        sudo cp mir.h "$SYSTEM_PREFIX/include/" 2>/dev/null || true
        sudo cp mir-*.h "$SYSTEM_PREFIX/include/" 2>/dev/null || true
        
        # Update library cache
        sudo ldconfig
        
        # Verify the build
        if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ]; then
            echo "✅ MIR built successfully"
            cd - > /dev/null
            return 0
        fi
    fi
    
    echo "❌ MIR build failed"
    cd - > /dev/null
    return 1
}

# Function to build criterion for Linux
build_criterion_for_linux() {
    echo "Building Criterion for Linux..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libcriterion.a" ] || [ -f "$SYSTEM_PREFIX/lib/libcriterion.so" ]; then
        echo "Criterion already installed in system location"
        return 0
    fi
    
    # Build from source
    if [ ! -d "build_temp/Criterion" ]; then
        cd build_temp
        echo "Cloning Criterion repository..."
        git clone https://github.com/Snaipe/Criterion.git || {
            echo "Warning: Could not clone Criterion repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "build_temp/Criterion"
    
    # Create build directory
    mkdir -p build
    cd build
    
    echo "Configuring Criterion with CMake..."
    if cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" \
        ..; then
        
        echo "Building Criterion..."
        if make -j$(nproc); then
            echo "Installing Criterion to system location (requires sudo)..."
            sudo make install
            
            # Update library cache
            sudo ldconfig
            
            # Verify the build
            if [ -f "$SYSTEM_PREFIX/lib/libcriterion.a" ] || [ -f "$SYSTEM_PREFIX/lib/libcriterion.so" ]; then
                echo "✅ Criterion built successfully"
                cd - > /dev/null
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    
    echo "❌ Criterion build failed"
    cd - > /dev/null
    cd - > /dev/null
    return 1
}

echo "Found native compiler: $(which gcc)"
echo "System: $(uname -a)"

# Build tree-sitter for Linux
if [ ! -f "lambda/tree-sitter/libtree-sitter.a" ]; then
    echo "Building tree-sitter for Linux..."
    cd lambda/tree-sitter
    
    # Clean previous builds
    make clean || true
    
    # Build static library for Linux
    make libtree-sitter.a
    
    cd - > /dev/null
    echo "Tree-sitter built successfully"
else
    echo "Tree-sitter already built for Linux"
fi

# Build tree-sitter-lambda for Linux
if [ ! -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
    echo "Building tree-sitter-lambda for Linux..."
    cd lambda/tree-sitter-lambda
    
    # Clean previous builds
    make clean || true
    
    # Build static library for Linux (creates libtree-sitter-lambda.a)
    make libtree-sitter-lambda.a
    
    cd - > /dev/null
    echo "Tree-sitter-lambda built successfully"
else
    echo "Tree-sitter-lambda already built for Linux"
fi

# Build lexbor for Linux
echo "Setting up lexbor..."
if [ -f "$SYSTEM_PREFIX/lib/liblexbor_static.a" ] || [ -f "$SYSTEM_PREFIX/lib/liblexbor.a" ]; then
    echo "lexbor already available"
elif dpkg -l | grep -q liblexbor-dev; then
    echo "lexbor already installed via apt"
else
    if ! build_lexbor_for_linux; then
        echo "Warning: lexbor build failed"
        exit 1
    else
        echo "lexbor built successfully"
    fi
fi

# Build GMP for Linux  
echo "Setting up GMP..."
if [ -f "$SYSTEM_PREFIX/lib/libgmp.a" ] || [ -f "$SYSTEM_PREFIX/lib/libgmp.so" ]; then
    echo "GMP already available"
elif dpkg -l | grep -q libgmp-dev; then
    echo "GMP already installed via apt"
else
    if ! build_gmp_for_linux; then
        echo "Warning: GMP build failed"
        exit 1
    else
        echo "GMP built successfully"
    fi
fi

# Build MIR for Linux
echo "Setting up MIR..."
if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ]; then
    echo "MIR already available"
else
    if ! build_mir_for_linux; then
        echo "Warning: MIR build failed"
        exit 1
    else
        echo "MIR built successfully"
    fi
fi

# Build utf8proc for Linux
echo "Setting up utf8proc..."
if [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ]; then
    echo "utf8proc already available"
else
    if ! build_utf8proc_for_linux; then
        echo "Warning: utf8proc build failed"
        exit 1
    else
        echo "utf8proc built successfully"
    fi
fi

# Install apt dependencies that are required by build_lambda_config.json
echo "Installing apt dependencies..."

# Required dependencies from build_lambda_config.json
APT_DEPS=(
    "coreutils"        # For timeout command needed by test suite
)

for dep in "${APT_DEPS[@]}"; do
    echo "Installing $dep..."
    if dpkg -l | grep -q "$dep"; then
        echo "$dep already installed"
    else
        if sudo apt install -y "$dep"; then
            echo "✅ $dep installed successfully"
        else
            echo "❌ Failed to install $dep"
            exit 1
        fi
    fi
done

# Build criterion for Linux (build from source if apt package not available)
echo "Setting up criterion..."
if [ -f "$SYSTEM_PREFIX/lib/libcriterion.a" ] || [ -f "$SYSTEM_PREFIX/lib/libcriterion.so" ]; then
    echo "criterion already available"
elif dpkg -l | grep -q libcriterion-dev; then
    echo "criterion already installed via apt"
else
    if ! build_criterion_for_linux; then
        echo "Warning: criterion build failed"
        exit 1
    else
        echo "criterion built successfully"
    fi
fi

# Clean up intermediate files
cleanup_intermediate_files

echo "Linux (Ubuntu) native compilation setup completed!"
echo ""
echo "Built dependencies:"
echo "- Tree-sitter: $([ -f "lambda/tree-sitter/libtree-sitter.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Tree-sitter-lambda: $([ -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ] && echo "✓ Built" || echo "✗ Missing")"

# Check system locations and apt packages
echo "- GMP: $([ -f "$SYSTEM_PREFIX/lib/libgmp.a" ] || [ -f "$SYSTEM_PREFIX/lib/libgmp.so" ] || [ -f "/usr/lib/x86_64-linux-gnu/libgmp.so" ] && echo "✓ Available" || echo "✗ Missing")"
echo "- lexbor: $([ -f "$SYSTEM_PREFIX/lib/liblexbor_static.a" ] || [ -f "$SYSTEM_PREFIX/lib/liblexbor.a" ] || [ -f "$SYSTEM_PREFIX/lib/liblexbor.so" ] && echo "✓ Available" || echo "✗ Missing")"
echo "- MIR: $([ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- curl: $([ -f "/usr/lib/x86_64-linux-gnu/libcurl.so" ] || dpkg -l | grep -q libcurl && echo "✓ Available" || echo "✗ Missing")"
echo "- mpdecimal: $([ -f "/usr/lib/x86_64-linux-gnu/libmpdec.so" ] || dpkg -l | grep -q libmpdec-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- libedit: $([ -f "/usr/lib/x86_64-linux-gnu/libedit.so" ] || dpkg -l | grep -q libedit-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- utf8proc: $([ -f "/usr/lib/x86_64-linux-gnu/libutf8proc.so" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ] || dpkg -l | grep -q libutf8proc-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- libedit: $([ -f "/usr/lib/x86_64-linux-gnu/libedit.so" ] || dpkg -l | grep -q libedit-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- criterion: $([ -f "$SYSTEM_PREFIX/lib/libcriterion.a" ] || [ -f "$SYSTEM_PREFIX/lib/libcriterion.so" ] || dpkg -l | grep -q libcriterion-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- coreutils: $(command -v timeout >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
echo ""
echo "Next steps:"
echo "1. Run: ./compile.sh"
echo ""
echo "To clean up intermediate files later, run: ./setup-linux-deps.sh clean"
