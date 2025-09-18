#!/bin/bash

# Linux (Ubuntu) native compilation dependency setup script
# Updated: Installs/builds all libraries and dev_libraries from build config
set -e

SCRIPT_DIR="$(pwd)"
# Install dependencies to system locations that build_lambda_config.json expects
SYSTEM_PREFIX="/usr/local"

# List of libraries and dev_libraries from build_lambda_config.json
ALL_LIBS=(
    "libcurl4-openssl-dev"   # curl
    "libedit-dev"            # libedit
    "libmpdec-dev"           # mpdecimal
    "libutf8proc-dev"        # utf8proc
    "libssl-dev"             # ssl
    "zlib1g-dev"             # z
    "libnghttp2-dev"         # nghttp2
    "libncurses5-dev"        # ncurses (provides tinfo)
    "libbsd-dev"             # bsd
    "libmd-dev"              # md
    "build-essential"        # includes pthread
)

# Function to build libharu from source
build_libharu_for_linux() {
    echo "Building libharu from source..."
    mkdir -p build_temp
    cd build_temp
    if [ ! -d "libharu" ]; then
        git clone https://github.com/libharu/libharu.git || {
            echo "Failed to clone libharu repository."
            cd - > /dev/null
            return 1
        }
    fi
    cd libharu
    mkdir -p build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" .. && \
    make -j$(nproc) && \
    sudo make install && \
    sudo ldconfig
    cd - > /dev/null
    cd - > /dev/null
    cd - > /dev/null
    echo "libharu built and installed."
}

# Catch2 is not available via apt, build from source
CATCH2_VERSION="3.10.0"
CATCH2_REPO="https://github.com/catchorg/Catch2.git"
build_catch2_for_linux() {
    echo "Building Catch2 for Linux..."
    mkdir -p build_temp
    if [ ! -d "build_temp/Catch2" ]; then
        cd build_temp
        git clone --branch v$CATCH2_VERSION $CATCH2_REPO Catch2 || {
            echo "Warning: Could not clone Catch2 repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    cd build_temp/Catch2
    mkdir -p build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" .. && \
    make -j$(nproc) && \
    sudo make install && \
    sudo ldconfig
    cd - > /dev/null
    cd - > /dev/null
    echo "Catch2 built and installed."
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

# Verify installation function
verify_installation() {
    local tool="$1"
    local package="$2"
    
    if command -v "$tool" >/dev/null 2>&1; then
        echo "✅ $tool verified and working"
        return 0
    else
        echo "❌ $tool not found after installing $package"
        return 1
    fi
}


# Function to check if a package is installed
is_package_installed() {
    local package="$1"
    dpkg -l | grep -q "^ii.*$package" 2>/dev/null
}

# Function to install package if not already installed
install_if_missing() {
    local package="$1"
    local description="${2:-$package}"
    # For certain packages, check if the command is available first
    case "$package" in
        "cmake"|"git"|"curl"|"wget"|"python3"|"vim-common"|"clang"|"nodejs"|"npm")
            local command_name="$package"
            if [ "$package" = "python3-pip" ]; then
                command_name="pip3"
            elif [ "$package" = "vim-common" ]; then
                command_name="xxd"
            elif [ "$package" = "nodejs" ]; then
                command_name="node"
            fi
            if command -v "$command_name" >/dev/null 2>&1; then
                echo "✅ $description already available"
                return 0
            fi
            ;;
    esac
    # Check if package is installed via apt
    if is_package_installed "$package"; then
        echo "✅ $description already installed"
        return 0
    else
        echo "Installing $description..."
        if sudo apt install -y "$package"; then
            echo "✅ $description installed successfully"
            return 0
        else
            echo "❌ Failed to install $description"
            return 1
        fi
    fi
}

# Check for required tools
check_and_install_tool() {
    local tool="$1"
    local package="$2"
    if command -v "$tool" >/dev/null 2>&1; then
        echo "✅ $tool already available"
        return 0
    else
        echo "Installing $tool via $package..."
        install_if_missing "$package" "$package"
        if command -v "$tool" >/dev/null 2>&1; then
            echo "✅ $tool installed successfully"
            return 0
        else
            echo "❌ Failed to install $tool via $package"
            return 1
        fi
    fi
}

# Check if running on Ubuntu/Debian
if ! command -v apt >/dev/null 2>&1; then
    echo "Warning: This script is designed for Ubuntu/Debian systems with apt package manager."
    echo "You may need to adapt the package installation commands for your distribution."
fi

# Check for essential build tools
echo "Setting up essential build tools..."
check_and_install_tool "make" "build-essential" || exit 1
check_and_install_tool "gcc" "build-essential" || exit 1
check_and_install_tool "g++" "build-essential" || exit 1
check_and_install_tool "git" "git" || exit 1

# Check for Node.js and npm (needed for tree-sitter CLI via npx)
echo "Setting up Node.js and npm for tree-sitter CLI..."

# Node.js install: try apt, then fallback to NodeSource setup script if needed
echo "Checking for Node.js (node)..."
if command -v node >/dev/null 2>&1; then
    echo "✅ node already available"
else
    echo "Trying to install nodejs via apt..."
    if sudo apt update && sudo apt install -y nodejs npm; then
        if command -v node >/dev/null 2>&1; then
            echo "✅ node installed via apt"
        else
            echo "❌ nodejs package installed but 'node' command not found. Trying NodeSource setup..."
            curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
            sudo apt install -y nodejs
        fi
    else
        echo "❌ Failed to install nodejs via apt. Trying NodeSource setup..."
        curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
        sudo apt install -y nodejs
    fi
    if command -v node >/dev/null 2>&1; then
        echo "✅ node available after NodeSource setup"
    else
        echo "❌ Node.js installation failed. Please install manually."
        exit 1
    fi
fi

echo "Checking for npm..."
if command -v npm >/dev/null 2>&1; then
    echo "✅ npm already available"
else
    echo "Trying to install npm via apt..."
    if sudo apt install -y npm; then
        echo "✅ npm installed via apt"
    else
        echo "❌ Failed to install npm via apt. Trying NodeSource setup..."
        curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
        sudo apt install -y npm
    fi
    if command -v npm >/dev/null 2>&1; then
        echo "✅ npm available after NodeSource setup"
    else
        echo "❌ npm installation failed. Please install manually."
        exit 1
    fi
fi

# Verify npx can access tree-sitter CLI
echo "Verifying tree-sitter CLI access via npx..."
if timeout 10 npx tree-sitter-cli@0.24.7 --version >/dev/null 2>&1; then
    echo "✅ Tree-sitter CLI 0.24.7 accessible via npx"
else
    echo "Warning: tree-sitter CLI may need to be downloaded on first use"
fi

# Check for cmake (needed for some dependencies)
if command -v cmake >/dev/null 2>&1; then
    echo "✅ cmake already available"
else
    echo "Installing cmake..."
    sudo apt update
    install_if_missing "cmake" "cmake"
fi

# Check for pkg-config (often needed for library detection)
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Installing pkg-config..."
    install_if_missing "pkg-config" "pkg-config"
else
    echo "✅ pkg-config already available"
fi

# Install additional development tools if not present
echo "Installing additional development dependencies..."
sudo apt update



# Function to build mpdecimal for Linux
build_mpdecimal_for_linux() {
    echo "Building mpdecimal for Linux..."
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/include/mpdecimal.h" ]; then
        echo "mpdecimal already installed in system location"
        return 0
    fi
    # Create build_temp directory if it doesn't exist
    mkdir -p "build_temp"
    # Build from source
    if [ ! -d "build_temp/mpdecimal" ]; then
        cd build_temp
        echo "Downloading mpdecimal..."
        curl -L "https://www.bytereef.org/software/mpdecimal/releases/mpdecimal-2.5.1.tar.gz" -o "mpdecimal-2.5.1.tar.gz"
        tar -xzf "mpdecimal-2.5.1.tar.gz"
        mv "mpdecimal-2.5.1" "mpdecimal"
        rm "mpdecimal-2.5.1.tar.gz"
        cd - > /dev/null
    fi
    cd "build_temp/mpdecimal"
    echo "Configuring mpdecimal..."
    if ./configure --prefix="$SYSTEM_PREFIX"; then
        echo "Building mpdecimal..."
        if make -j$(nproc); then
            echo "Installing mpdecimal to system location (requires sudo)..."
            sudo make install
            # Update library cache
            sudo ldconfig
            # Verify the build
            if [ -f "$SYSTEM_PREFIX/include/mpdecimal.h" ]; then
                echo "✅ mpdecimal built and installed successfully"
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    echo "❌ mpdecimal build failed"
    cd - > /dev/null
    return 1
}

# Install all libraries and dev_libraries from config

# Install all libraries and dev_libraries from config
for lib in "${ALL_LIBS[@]}"; do
    if [ "$lib" = "libmpdec-dev" ]; then
        if ! install_if_missing "$lib" "$lib"; then
            echo "Falling back to building mpdecimal from source..."
            build_mpdecimal_for_linux
        fi
    else
        install_if_missing "$lib" "$lib"
    fi
done

# Build libharu from source (not available in repos)
if ! build_libharu_for_linux; then
    echo "Warning: libharu build failed"
fi


# Function to build libharu from source
build_libharu_for_linux() {
    echo "Building libharu from source..."
    mkdir -p build_temp
    cd build_temp
    if [ ! -d "libharu" ]; then
        git clone https://github.com/libharu/libharu.git || {
            echo "Failed to clone libharu repository."
            cd - > /dev/null
            return 1
        }
    fi
    cd libharu
    mkdir -p build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" .. && \
    make -j$(nproc) && \
    sudo make install && \
    sudo ldconfig
    cd - > /dev/null
    cd - > /dev/null
    cd - > /dev/null
    echo "libharu built and installed."
}

# Build Catch2 if not available
if ! pkg-config --exists catch2; then
    build_catch2_for_linux
fi

# Build Criterion if not available  
if ! dpkg -l | grep -q libcriterion-dev; then
    if ! build_criterion_for_linux; then
        echo "Warning: criterion build failed"
    fi
fi

# Verify mpdecimal header installation
echo "Verifying mpdecimal header installation..."
if [ -f "/usr/include/mpdecimal.h" ]; then
    echo "✅ mpdecimal.h found at /usr/include/mpdecimal.h"
elif [ -f "/usr/include/mpdec/mpdecimal.h" ]; then
    echo "✅ mpdecimal.h found at /usr/include/mpdec/mpdecimal.h"
    echo "Creating symlink for standard location..."
    sudo ln -sf /usr/include/mpdec/mpdecimal.h /usr/include/mpdecimal.h
elif [ -f "/usr/local/include/mpdecimal.h" ]; then
    echo "✅ mpdecimal.h found at /usr/local/include/mpdecimal.h"
else
    echo "❌ mpdecimal.h not found after installation"
    echo "Searching for mpdecimal headers..."
    find /usr -name "*mpdec*" -type f 2>/dev/null || echo "No mpdecimal files found"
    # Directly build from source if header is missing
    echo "Building mpdecimal from source..."
    build_mpdecimal_for_linux
fi

# Try to install criterion via apt, build from source if not available
echo "Installing criterion testing framework..."
# Note: libcriterion-dev package installation is handled by build function below

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

# Function to build mpdecimal for Linux
build_mpdecimal_for_linux() {
    echo "Building mpdecimal for Linux..."
    
    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/include/mpdecimal.h" ]; then
        echo "mpdecimal already installed in system location"
        return 0
    fi
    
    # Create build_temp directory if it doesn't exist
    mkdir -p "build_temp"
    
    # Build from source
    if [ ! -d "build_temp/mpdecimal" ]; then
        cd build_temp
        echo "Downloading mpdecimal..."
        curl -L "https://www.bytereef.org/software/mpdecimal/releases/mpdecimal-2.5.1.tar.gz" -o "mpdecimal-2.5.1.tar.gz"
        tar -xzf "mpdecimal-2.5.1.tar.gz"
        mv "mpdecimal-2.5.1" "mpdecimal"
        rm "mpdecimal-2.5.1.tar.gz"
        cd - > /dev/null
    fi
    
    cd "build_temp/mpdecimal"
    
    echo "Configuring mpdecimal..."
    if ./configure --prefix="$SYSTEM_PREFIX"; then
        echo "Building mpdecimal..."
        if make -j$(nproc); then
            echo "Installing mpdecimal to system location (requires sudo)..."
            sudo make install
            
            # Update library cache
            sudo ldconfig
            
            # Verify the build
            if [ -f "$SYSTEM_PREFIX/include/mpdecimal.h" ]; then
                echo "✅ mpdecimal built and installed successfully"
                cd - > /dev/null
                return 0
            fi
        fi
    fi
    
    echo "❌ mpdecimal build failed"
    cd - > /dev/null
    return 1
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
        
        # Copy library (try different names)
        if [ -f "libmir.a" ]; then
            sudo cp libmir.a "$SYSTEM_PREFIX/lib/"
        elif [ -f "mir.a" ]; then
            sudo cp mir.a "$SYSTEM_PREFIX/lib/libmir.a"
        else
            echo "Warning: MIR library not found"
            find . -name "*.a" -name "*mir*" | head -5
        fi
        
        # Copy headers (more comprehensive approach)
        if [ -f "mir.h" ]; then
            sudo cp mir.h "$SYSTEM_PREFIX/include/"
            echo "✅ mir.h copied to $SYSTEM_PREFIX/include/"
        else
            echo "Warning: mir.h not found"
            find . -name "mir.h" | head -5
        fi
        
        # Copy all MIR-related headers
        for header in mir-*.h; do
            if [ -f "$header" ]; then
                sudo cp "$header" "$SYSTEM_PREFIX/include/"
                echo "✅ $header copied to $SYSTEM_PREFIX/include/"
            fi
        done
        
        # Update library cache
        sudo ldconfig
        
        # Verify the build
        if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ]; then
            echo "✅ MIR built successfully"
            cd - > /dev/null
            return 0
        elif [ -f "$SYSTEM_PREFIX/lib/libmir.a" ]; then
            echo "⚠️ MIR library built but mir.h header missing"
            cd - > /dev/null
            return 1
        else
            echo "❌ MIR library not found after build"
            cd - > /dev/null
            return 1
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
    if [ -d "lambda/tree-sitter" ]; then
        echo "Building tree-sitter for Linux..."
        cd lambda/tree-sitter
        # Clean previous builds
        make clean || true
        # Build static library for Linux
        make libtree-sitter.a
        cd - > /dev/null
        echo "Tree-sitter built successfully"
    else
        echo "❌ Directory lambda/tree-sitter does not exist. Skipping tree-sitter build."
        echo "Please ensure the tree-sitter source is present at lambda/tree-sitter."
    fi
else
    echo "Tree-sitter already built for Linux"
fi

# Build tree-sitter-lambda for Linux
if [ ! -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
    if [ -d "lambda/tree-sitter-lambda" ]; then
        echo "Building tree-sitter-lambda for Linux..."
        cd lambda/tree-sitter-lambda
        # Clean previous builds
        make clean || true
        # Build static library for Linux (creates libtree-sitter-lambda.a)
        make libtree-sitter-lambda.a
        cd - > /dev/null
        echo "Tree-sitter-lambda built successfully"
    else
        echo "❌ Directory lambda/tree-sitter-lambda does not exist. Skipping tree-sitter-lambda build."
        echo "Please ensure the tree-sitter-lambda source is present at lambda/tree-sitter-lambda."
    fi
else
    echo "Tree-sitter-lambda already built for Linux"
fi


# Build lexbor for Linux (if used, not in config but referenced in mac script)
if [ -f "$SYSTEM_PREFIX/lib/liblexbor_static.a" ] || [ -f "$SYSTEM_PREFIX/lib/liblexbor.a" ]; then
    echo "lexbor already available"
elif dpkg -l | grep -q liblexbor-dev; then
    echo "lexbor already installed via apt"
else
    if ! build_lexbor_for_linux; then
        echo "Warning: lexbor build failed"
    else
        echo "lexbor built successfully"
    fi
fi


# Build GMP for Linux (if used)
if [ -f "$SYSTEM_PREFIX/lib/libgmp.a" ] || [ -f "$SYSTEM_PREFIX/lib/libgmp.so" ]; then
    echo "GMP already available"
elif dpkg -l | grep -q libgmp-dev; then
    echo "GMP already installed via apt"
else
    if ! build_gmp_for_linux; then
        echo "Warning: GMP build failed"
    else
        echo "GMP built successfully"
    fi
fi


# Build MIR for Linux
if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ]; then
    echo "MIR already available"
else
    if ! build_mir_for_linux; then
        echo "Warning: MIR build failed"
    else
        echo "MIR built successfully"
    fi
fi

# Verify MIR header installation (but don't exit on failure)
echo "Verifying MIR header installation..."
if [ -f "$SYSTEM_PREFIX/include/mir.h" ]; then
    echo "✅ mir.h found at $SYSTEM_PREFIX/include/mir.h"
elif [ -f "/usr/include/mir.h" ]; then
    echo "✅ mir.h found at /usr/include/mir.h"
else
    echo "⚠️ mir.h not found - MIR may need to be built during compilation"
    echo "Searching for MIR headers..."
    find /usr -name "mir.h" 2>/dev/null || echo "No mir.h files found"
    find "$SYSTEM_PREFIX" -name "mir.h" 2>/dev/null || echo "No mir.h files found in $SYSTEM_PREFIX"
fi


# Build utf8proc for Linux
if [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ] || [ -f "/usr/lib/x86_64-linux-gnu/libutf8proc.so" ] || dpkg -l | grep -q libutf8proc-dev; then
    echo "utf8proc already available"
else
    if ! build_utf8proc_for_linux; then
        echo "Warning: utf8proc build failed"
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
        echo "✅ $dep already installed"
    else
        install_if_missing "$dep" "$dep"
    fi
done

# Install premake5 from source if not available
if command -v premake5 >/dev/null 2>&1; then
    echo "✅ premake5 already available"
else
    echo "Installing premake5 from source..."
    PREMAKE_BUILD_DIR="build_temp/premake5"
    if [ ! -d "$PREMAKE_BUILD_DIR" ]; then
        mkdir -p build_temp
        cd build_temp
        
        # Clone and build premake5 from source (works on all architectures)
        echo "Cloning premake5 source..."
        if git clone --recurse-submodules https://github.com/premake/premake-core.git premake5; then
            cd premake5
            
            # Build premake5 using make
            echo "Building premake5..."
            if make -f Bootstrap.mak linux; then
                # Install the binary
                if sudo cp bin/release/premake5 /usr/local/bin/ && sudo chmod +x /usr/local/bin/premake5; then
                    echo "✅ premake5 installed successfully"
                else
                    echo "❌ Failed to install premake5"
                fi
            else
                echo "❌ Failed to build premake5"
            fi
        else
            echo "❌ Failed to clone premake5"
        fi
        
        cd "$SCRIPT_DIR"
    else
        echo "✅ premake5 build directory already exists"
    fi
fi


# Build criterion for Linux (build from source if apt package not available)
if [ -f "$SYSTEM_PREFIX/lib/libcriterion.a" ] || [ -f "$SYSTEM_PREFIX/lib/libcriterion.so" ] || dpkg -l | grep -q libcriterion-dev; then
    echo "criterion already available"
else
    if ! build_criterion_for_linux; then
        echo "Warning: criterion build failed"
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
echo "- MIR: $([ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- curl: $([ -f "/usr/lib/x86_64-linux-gnu/libcurl.so" ] || dpkg -l | grep -q libcurl && echo "✓ Available" || echo "✗ Missing")"
echo "- mpdecimal: $([ -f "/usr/lib/x86_64-linux-gnu/libmpdec.so" ] || dpkg -l | grep -q libmpdec-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- libedit: $([ -f "/usr/lib/x86_64-linux-gnu/libedit.so" ] || dpkg -l | grep -q libedit-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- utf8proc: $([ -f "/usr/lib/x86_64-linux-gnu/libutf8proc.so" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ] || dpkg -l | grep -q libutf8proc-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- tinfo: $([ -f "/usr/lib/x86_64-linux-gnu/libtinfo.so" ] || dpkg -l | grep -q libncurses && echo "✓ Available" || echo "✗ Missing")"
echo "- bsd: $([ -f "/usr/lib/x86_64-linux-gnu/libbsd.so" ] || dpkg -l | grep -q libbsd-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- md: $([ -f "/usr/lib/x86_64-linux-gnu/libmd.so" ] || dpkg -l | grep -q libmd-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- criterion: $([ -f "$SYSTEM_PREFIX/lib/libcriterion.a" ] || [ -f "$SYSTEM_PREFIX/lib/libcriterion.so" ] || dpkg -l | grep -q libcriterion-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- coreutils: $(command -v timeout >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
echo "- premake5: $(command -v premake5 >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
echo ""
echo "Next steps:"
echo "1. Run: ./compile.sh"
echo ""
echo "Note: This script now intelligently skips already-installed dependencies."
echo "To clean up intermediate files, run: ./setup-linux-deps.sh clean"
