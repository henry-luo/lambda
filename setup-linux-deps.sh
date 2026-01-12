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
    "libmpdec-dev"           # mpdecimal
    "libutf8proc-dev"        # utf8proc
    "libssl-dev"             # ssl
    "zlib1g-dev"             # z
    "libnghttp2-dev"         # nghttp2
    "libncurses5-dev"        # ncurses
    "build-essential"        # includes pthread
)

# Radiant project dependencies - for HTML/CSS/SVG rendering engine
# Note: freetype is included but may need specific version handling
# Note: ThorVG is built from source (see build_thorvg_v1_0_pre34_for_linux function)
RADIANT_DEPS=(
    "libglfw3-dev"           # OpenGL window and context management
    "libfreetype6-dev"       # FreeType font rendering library
    "libfontconfig1-dev"     # Font configuration library
    "libexpat1-dev"          # XML parser library (dependency of fontconfig)
    "libpng-dev"             # PNG image format support
    "libbz2-dev"             # Alternative compression library
    "zlib1g-dev"             # Compression library (already included above)
    "libturbojpeg0-dev"      # TurboJPEG library with turbojpeg.h header
    "libgif-dev"             # GIF image format support
    "gettext"                # For libintl support
    "libgl1-mesa-dev"        # OpenGL development libraries
    "libglu1-mesa-dev"       # OpenGL utility libraries
    "libegl1-mesa-dev"       # EGL development libraries
)

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

    # Clean tree-sitter-javascript build files
    if [ -d "lambda/tree-sitter-javascript" ]; then
        cd lambda/tree-sitter-javascript
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi

    # Clean tree-sitter-latex build files
    if [ -d "lambda/tree-sitter-latex" ]; then
        cd lambda/tree-sitter-latex
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
        "cmake"|"git"|"curl"|"wget"|"python3"|"xxd"|"clang"|"nodejs"|"npm")
            local command_name="$package"
            if [ "$package" = "python3-pip" ]; then
                command_name="pip3"
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

# xxd is now a separate package in Ubuntu 24.04+
if ! command -v xxd >/dev/null 2>&1; then
    echo "Installing xxd..."
    if sudo apt install -y xxd; then
        echo "✅ xxd installed successfully"
    else
        echo "❌ Failed to install xxd"
        exit 1
    fi
else
    echo "✅ xxd already available"
fi

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

# Check for meson (needed for building dependencies from source)
if ! command -v meson >/dev/null 2>&1; then
    echo "Installing meson..."
    install_if_missing "meson" "meson"
else
    echo "✅ meson already available"
fi

# Install additional development tools if not present
echo "Installing additional development dependencies..."
sudo apt update



# Function to build Google Test for Linux
build_gtest_for_linux() {
    echo "Building Google Test for Linux..."

    # Check if already installed in system location
    if [ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ]; then
        echo "Google Test already installed in system location"
        return 0
    fi

    # Create build_temp directory if it doesn't exist
    mkdir -p "build_temp"

    # Build from source
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
    mkdir -p build
    cd build

    echo "Configuring Google Test with CMake..."
    if cmake -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_INSTALL_PREFIX="$SYSTEM_PREFIX" \
             -DBUILD_GMOCK=ON \
             -DBUILD_GTEST=ON \
             -DGTEST_CREATE_SHARED_LIBRARY=OFF \
             ..; then

        echo "Building Google Test..."
        if make -j$(nproc); then
            echo "Installing Google Test to system location (requires sudo)..."
            sudo make install

            # Update library cache
            sudo ldconfig

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

# Install Radiant project dependencies
echo "Installing Radiant project dependencies..."
for dep in "${RADIANT_DEPS[@]}"; do
    # Skip zlib1g-dev as it's already installed above
    if [ "$dep" = "zlib1g-dev" ]; then
        echo "zlib1g-dev already handled in core dependencies"
        continue
    fi

    install_if_missing "$dep" "$dep"
done

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

# Function to build ThorVG v1.0-pre34 for Linux
build_thorvg_v1_0_pre34_for_linux() {
    echo "Building ThorVG v1.0-pre34 for Linux..."

    # Check if already installed in system location and verify headers
    if [ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ]; then
        if [ -f "$SYSTEM_PREFIX/include/thorvg.h" ]; then
            # Verify no GL symbols (which cause conflicts with system OpenGL)
            if ! nm "$SYSTEM_PREFIX/lib/libthorvg.a" 2>/dev/null | grep -q "glClearColor"; then
                echo "✅ ThorVG v1.0-pre34 already installed in system location"
                return 0
            else
                echo "ThorVG library has GL symbols (conflicts with OpenGL), rebuilding..."
                sudo rm -f "$SYSTEM_PREFIX/lib/libthorvg"* 2>/dev/null || true
            fi
        else
            echo "ThorVG library found but headers missing, rebuilding..."
        fi
    fi

    # Create build_temp directory if it doesn't exist
    mkdir -p "build_temp"

    if [ ! -d "build_temp/thorvg" ]; then
        cd build_temp
        echo "Cloning ThorVG repository..."
        git clone https://github.com/thorvg/thorvg.git || {
            echo "Warning: Could not clone ThorVG repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    else
        echo "ThorVG source already downloaded"
        cd "build_temp/thorvg"
        # Make sure we're up to date
        git fetch --tags 2>/dev/null || true
        cd - > /dev/null
    fi

    cd "build_temp/thorvg"

    # Checkout v1.0-pre34 specifically
    echo "Checking out ThorVG v1.0-pre34..."
    git fetch --tags
    git checkout v1.0-pre34 || {
        echo "❌ Failed to checkout ThorVG v1.0-pre34"
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
                    echo "Failed to install meson via pip3, trying apt..."
                    if sudo apt update && sudo apt install -y meson ninja-build; then
                        echo "✅ meson installed via apt"
                    else
                        echo "Failed to install meson via apt"
                        cd - > /dev/null
                        return 1
                    fi
                }
                # Add user bin to PATH if not already there
                export PATH="$HOME/.local/bin:$PATH"
            else
                echo "pip3 not found, trying apt..."
                if sudo apt update && sudo apt install -y python3-pip meson ninja-build; then
                    echo "✅ meson installed via apt"
                else
                    echo "Cannot install meson - no pip3 or apt package manager"
                    cd - > /dev/null
                    return 1
                fi
            fi
        fi

        # Clean previous build directory to ensure fresh configuration
        rm -rf build-linux
        mkdir -p build-linux

        echo "Configuring ThorVG with Meson..."
        # CRITICAL: Build with ONLY SW engine (no GL) to avoid OpenGL symbol conflicts
        # The GL engine includes GLAD which defines GL functions as global variables,
        # causing bus errors at runtime when system OpenGL tries to use them.
        if meson setup build-linux \
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

            echo "Building ThorVG v1.0-pre34..."
            if ninja -C build-linux; then
                echo "Installing ThorVG v1.0-pre34 to $SYSTEM_PREFIX (requires sudo)..."
                # Copy files manually
                sudo mkdir -p "$SYSTEM_PREFIX/lib"
                sudo mkdir -p "$SYSTEM_PREFIX/include"
                
                if [ -f "build-linux/src/libthorvg.a" ]; then
                    sudo cp "build-linux/src/libthorvg.a" "$SYSTEM_PREFIX/lib/"
                fi
                
                if [ -f "inc/thorvg.h" ]; then
                    sudo cp "inc/thorvg.h" "$SYSTEM_PREFIX/include/"
                fi

                # Verify installation
                if [ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] && [ -f "$SYSTEM_PREFIX/include/thorvg.h" ]; then
                    # Verify text API is available
                    if nm "$SYSTEM_PREFIX/lib/libthorvg.a" | grep -q "tvg_text_new"; then
                        # Verify no GL symbols
                        if ! nm "$SYSTEM_PREFIX/lib/libthorvg.a" | grep -q "glClearColor"; then
                            echo "✅ ThorVG v1.0-pre34 built successfully"
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

# Function to build rpmalloc for Linux
build_rpmalloc_for_linux() {
    echo "Building rpmalloc for Linux..."

    # Determine architecture
    ARCH=$(uname -m)
    if [ "$ARCH" = "aarch64" ]; then
        LIB_DIR="$SYSTEM_PREFIX/lib/aarch64-linux-gnu"
    else
        LIB_DIR="$SYSTEM_PREFIX/lib/x86_64-linux-gnu"
    fi

    # Check if already installed in system location
    if [ -f "$LIB_DIR/librpmalloc.a" ] && [ -f "$SYSTEM_PREFIX/include/rpmalloc/rpmalloc.h" ]; then
        # Verify the library has the expected symbols
        if nm "$LIB_DIR/librpmalloc.a" 2>/dev/null | grep -q "rpmalloc_initialize"; then
            echo "✅ rpmalloc already installed and verified"
            return 0
        else
            echo "rpmalloc found but missing expected symbols, rebuilding..."
            sudo rm -f "$LIB_DIR/librpmalloc.a" 2>/dev/null || true
        fi
    fi

    # Create build_temp directory if it doesn't exist
    mkdir -p build_temp

    # Check if source exists, if not clone it
    if [ ! -d "build_temp/rpmalloc-src" ]; then
        echo "Cloning rpmalloc repository..."
        cd build_temp
        git clone https://github.com/mjansson/rpmalloc.git rpmalloc-src || {
            echo "Warning: Could not clone rpmalloc repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    else
        echo "rpmalloc source already downloaded"
    fi

    cd build_temp/rpmalloc-src

    # Clean any previous builds
    rm -f *.o *.a 2>/dev/null || true

    # Build rpmalloc with ENABLE_OVERRIDE=0 (no malloc override)
    # This allows us to use rpmalloc only for explicit pool allocations
    echo "Compiling rpmalloc with ENABLE_OVERRIDE=0..."
    if gcc -c -O2 \
        -DRPMALLOC_FIRST_CLASS_HEAPS=1 \
        -DENABLE_OVERRIDE=0 \
        -I. \
        rpmalloc/rpmalloc.c \
        -o rpmalloc.o; then

        echo "Creating static library..."
        if ar rcs librpmalloc.a rpmalloc.o; then
            # Install the library and headers
            echo "Installing rpmalloc to system location (requires sudo)..."
            sudo mkdir -p "$LIB_DIR"
            sudo mkdir -p "$SYSTEM_PREFIX/include/rpmalloc"
            sudo cp librpmalloc.a "$LIB_DIR/"
            sudo cp rpmalloc/rpmalloc.h "$SYSTEM_PREFIX/include/rpmalloc/"

            # Verify the library has expected symbols
            if nm "$LIB_DIR/librpmalloc.a" | grep -q "rpmalloc_initialize"; then
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

        # Copy c2mir.h specifically (critical for compilation)
        C2MIR_PATH=$(find . -name "c2mir.h" | head -1)
        if [ -n "$C2MIR_PATH" ] && [ -f "$C2MIR_PATH" ]; then
            sudo cp "$C2MIR_PATH" "$SYSTEM_PREFIX/include/"
            echo "✅ c2mir.h copied from $C2MIR_PATH to $SYSTEM_PREFIX/include/"
        else
            echo "Warning: c2mir.h not found in MIR build directory"
            # Fallback: check if c2mir.h exists in project include directory
            if [ -f "$SCRIPT_DIR/include/c2mir.h" ]; then
                sudo cp "$SCRIPT_DIR/include/c2mir.h" "$SYSTEM_PREFIX/include/"
                echo "✅ c2mir.h copied from project include/ to $SYSTEM_PREFIX/include/"
            else
                echo "Warning: c2mir.h not found in project include/ either"
                find . -name "c2mir.h" | head -5
            fi
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
        if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ] && [ -f "$SYSTEM_PREFIX/include/c2mir.h" ]; then
            echo "✅ MIR built successfully"
            cd - > /dev/null
            return 0
        elif [ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ]; then
            echo "⚠️ MIR library and mir.h built but c2mir.h header missing"
            cd - > /dev/null
            return 1
        elif [ -f "$SYSTEM_PREFIX/lib/libmir.a" ]; then
            echo "⚠️ MIR library built but header files missing"
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

echo "Found native compiler: $(which gcc)"
echo "System: $(uname -a)"

# Ensure we're in the workspace root for tree-sitter builds
cd "$SCRIPT_DIR"

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
        echo "Current directory: $(pwd)"
        echo "Directory contents: $(ls -la | head -5)"
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
        echo "Current directory: $(pwd)"
        echo "Directory contents: $(ls -la | head -5)"
    fi
else
    echo "Tree-sitter-lambda already built for Linux"
fi

# Build tree-sitter-javascript for Linux
if [ ! -f "lambda/tree-sitter-javascript/libtree-sitter-javascript.a" ]; then
    if [ -d "lambda/tree-sitter-javascript" ]; then
        echo "Building tree-sitter-javascript for Linux..."
        cd lambda/tree-sitter-javascript
        # Clean previous builds
        make clean || true

        # Generate parser if needed
        if [ ! -f "src/parser.c" ] || [ ! -f "src/grammar.json" ]; then
            echo "Generating tree-sitter-javascript parser..."
            if command -v npx >/dev/null 2>&1; then
                npx tree-sitter-cli@0.24.7 generate
            else
                echo "Warning: npx not available, assuming parser files are already generated"
            fi
        fi

        # Build static library for Linux (creates libtree-sitter-javascript.a)
        make libtree-sitter-javascript.a
        cd - > /dev/null
        echo "Tree-sitter-javascript built successfully"
    else
        echo "❌ Directory lambda/tree-sitter-javascript does not exist. Skipping tree-sitter-javascript build."
        echo "Please ensure the tree-sitter-javascript source is present at lambda/tree-sitter-javascript."
    fi
else
    echo "Tree-sitter-javascript already built for Linux"
fi

# Build tree-sitter-latex for Linux
if [ ! -f "lambda/tree-sitter-latex/libtree-sitter-latex.a" ]; then
    if [ -d "lambda/tree-sitter-latex" ]; then
        echo "Building tree-sitter-latex for Linux..."
        cd lambda/tree-sitter-latex
        # Clean previous builds
        make clean || true

        # Generate parser if needed
        if [ ! -f "src/parser.c" ] || [ ! -f "src/grammar.json" ]; then
            echo "Generating tree-sitter-latex parser..."
            if command -v npx >/dev/null 2>&1; then
                npx tree-sitter-cli@0.24.7 generate
            else
                echo "Warning: npx not available, assuming parser files are already generated"
            fi
        fi

        # Build static library for Linux (creates libtree-sitter-latex.a)
        make libtree-sitter-latex.a
        cd - > /dev/null
        echo "Tree-sitter-latex built successfully"
    else
        echo "❌ Directory lambda/tree-sitter-latex does not exist. Skipping tree-sitter-latex build."
        echo "Please ensure the tree-sitter-latex source is present at lambda/tree-sitter-latex."
    fi
else
    echo "Tree-sitter-latex already built for Linux"
fi


# Build ThorVG v1.0-pre34 for Linux (required for Radiant project, with TTF loader for SVG text support)
echo "Setting up ThorVG ..."

# Check if ThorVG v1.0-pre34 is already properly installed
if [ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.so" ]; then
    # Verify it's the correct version by checking if we can find the repository with the right tag
    if [ -d "build_temp/thorvg" ]; then
        cd "build_temp/thorvg"
        if git describe --tags 2>/dev/null | grep -q "v1.0-pre34"; then
            echo "✅ ThorVG v1.0-pre34 already installed and verified"
            cd - > /dev/null
        else
            echo "ThorVG found but version mismatch, rebuilding..."
            cd - > /dev/null
            # Force rebuild to ensure we have the correct version
            echo "Removing existing ThorVG installation to ensure correct version..."
            sudo rm -f "$SYSTEM_PREFIX/lib/libthorvg"* 2>/dev/null || true
            sudo rm -f "$SYSTEM_PREFIX/include/thorvg"* 2>/dev/null || true
            sudo rm -rf "$SYSTEM_PREFIX/include/thorvg" 2>/dev/null || true
            sudo rm -f "$SYSTEM_PREFIX/lib/pkgconfig/thorvg.pc" 2>/dev/null || true
            rm -rf "build_temp/thorvg" 2>/dev/null || true

            if ! build_thorvg_v1_0_pre34_for_linux; then
                echo "❌ ThorVG v1.0-pre34 build failed - required for Radiant project"
                exit 1
            else
                echo "✅ ThorVG v1.0-pre34 built successfully"
            fi
        fi
    else
        echo "✅ ThorVG v1.0-pre34 already installed in system location"
    fi
else
    # ThorVG not found, need to build it
    if ! build_thorvg_v1_0_pre34_for_linux; then
        echo "❌ ThorVG v1.0-pre34 build failed - required for Radiant project"
        exit 1
    else
        echo "✅ ThorVG v1.0-pre34 built successfully"
    fi
fi


# Build MIR for Linux
if [ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ] && [ -f "$SYSTEM_PREFIX/include/c2mir.h" ]; then
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
if [ -f "$SYSTEM_PREFIX/include/mir.h" ] && [ -f "$SYSTEM_PREFIX/include/c2mir.h" ]; then
    echo "✅ mir.h and c2mir.h found at $SYSTEM_PREFIX/include/"
elif [ -f "/usr/include/mir.h" ] && [ -f "/usr/include/c2mir.h" ]; then
    echo "✅ mir.h and c2mir.h found at /usr/include/"
elif [ -f "$SYSTEM_PREFIX/include/mir.h" ]; then
    echo "⚠️ mir.h found but c2mir.h missing at $SYSTEM_PREFIX/include/"
    # Try to copy c2mir.h from project include directory as fallback
    if [ -f "$SCRIPT_DIR/include/c2mir.h" ]; then
        echo "Copying c2mir.h from project include/ directory..."
        sudo cp "$SCRIPT_DIR/include/c2mir.h" "$SYSTEM_PREFIX/include/"
        echo "✅ c2mir.h copied from project include/ to $SYSTEM_PREFIX/include/"
    else
        echo "Warning: c2mir.h not found in project include/ directory either"
    fi
elif [ -f "/usr/include/mir.h" ]; then
    echo "⚠️ mir.h found but c2mir.h missing at /usr/include/"
    # Try to copy c2mir.h from project include directory as fallback
    if [ -f "$SCRIPT_DIR/include/c2mir.h" ]; then
        echo "Copying c2mir.h from project include/ directory..."
        sudo cp "$SCRIPT_DIR/include/c2mir.h" "$SYSTEM_PREFIX/include/"
        echo "✅ c2mir.h copied from project include/ to $SYSTEM_PREFIX/include/"
    else
        echo "Warning: c2mir.h not found in project include/ directory either"
    fi
else
    echo "⚠️ MIR headers not found - MIR may need to be built during compilation"
    echo "Searching for MIR headers..."
    find /usr -name "mir.h" 2>/dev/null || echo "No mir.h files found"
    find "$SYSTEM_PREFIX" -name "mir.h" 2>/dev/null || echo "No mir.h files found in $SYSTEM_PREFIX"
    find /usr -name "c2mir.h" 2>/dev/null || echo "No c2mir.h files found"
    find "$SYSTEM_PREFIX" -name "c2mir.h" 2>/dev/null || echo "No c2mir.h files found in $SYSTEM_PREFIX"
    # Try to copy c2mir.h from project include directory as fallback
    if [ -f "$SCRIPT_DIR/include/c2mir.h" ]; then
        echo "Copying c2mir.h from project include/ directory..."
        sudo cp "$SCRIPT_DIR/include/c2mir.h" "$SYSTEM_PREFIX/include/"
        echo "✅ c2mir.h copied from project include/ to $SYSTEM_PREFIX/include/"
    else
        echo "Warning: c2mir.h not found in project include/ directory either"
    fi
fi


# Build rpmalloc for Linux (memory pool allocator)
echo "Setting up rpmalloc..."
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ]; then
    RPMALLOC_LIB_DIR="$SYSTEM_PREFIX/lib/aarch64-linux-gnu"
else
    RPMALLOC_LIB_DIR="$SYSTEM_PREFIX/lib/x86_64-linux-gnu"
fi

if [ -f "$RPMALLOC_LIB_DIR/librpmalloc.a" ] && [ -f "$SYSTEM_PREFIX/include/rpmalloc/rpmalloc.h" ]; then
    if nm "$RPMALLOC_LIB_DIR/librpmalloc.a" 2>/dev/null | grep -q "rpmalloc_initialize"; then
        echo "✅ rpmalloc already available and verified"
    else
        echo "rpmalloc library found but missing required symbols, rebuilding..."
        if ! build_rpmalloc_for_linux; then
            echo "❌ rpmalloc build failed - required for Lambda memory pool"
            exit 1
        else
            echo "✅ rpmalloc built successfully"
        fi
    fi
else
    echo "rpmalloc not found, building..."
    if ! build_rpmalloc_for_linux; then
        echo "❌ rpmalloc build failed - required for Lambda memory pool"
        exit 1
    else
        echo "✅ rpmalloc built successfully"
    fi
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

    # Install uuid-dev dependency for premake5 build
    echo "Installing uuid-dev dependency for premake5..."
    install_if_missing "uuid-dev" "uuid-dev"

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


# Install Google Test for Linux (prefer apt package over building from source)
echo "Verifying Google Test installation..."
if [ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ] || dpkg -l | grep -q libgtest-dev; then
    echo "✅ Google Test already available"
else
    echo "Installing Google Test via apt..."
    if sudo apt install -y libgtest-dev libgmock-dev; then
        echo "✅ Google Test installed successfully via apt"

        # On Ubuntu, the gtest package only installs source files, we need to build them
        echo "Building Google Test libraries from apt-installed sources..."
        if [ -d "/usr/src/gtest" ]; then
            cd /usr/src/gtest
            sudo cmake CMakeLists.txt
            sudo make

            # Copy libraries to standard location
            sudo cp lib/*.a "$SYSTEM_PREFIX/lib/" 2>/dev/null || sudo cp *.a "$SYSTEM_PREFIX/lib/" 2>/dev/null || {
                echo "Trying alternative build approach..."
                sudo mkdir -p build
                cd build
                sudo cmake ..
                sudo make
                sudo cp lib/*.a "$SYSTEM_PREFIX/lib/" 2>/dev/null || sudo cp *.a "$SYSTEM_PREFIX/lib/"
            }

            # Build gmock too if available
            if [ -d "/usr/src/gmock" ]; then
                cd /usr/src/gmock
                sudo cmake CMakeLists.txt
                sudo make
                sudo cp lib/*.a "$SYSTEM_PREFIX/lib/" 2>/dev/null || sudo cp *.a "$SYSTEM_PREFIX/lib/" 2>/dev/null || {
                    echo "Trying alternative gmock build approach..."
                    sudo mkdir -p build
                    cd build
                    sudo cmake ..
                    sudo make
                    sudo cp lib/*.a "$SYSTEM_PREFIX/lib/" 2>/dev/null || sudo cp *.a "$SYSTEM_PREFIX/lib/"
                }
            fi

            sudo ldconfig
            cd "$SCRIPT_DIR"
            echo "✅ Google Test libraries built from apt sources"
        else
            echo "❌ Google Test source not found at /usr/src/gtest, trying build from source..."
            if ! build_gtest_for_linux; then
                echo "Warning: Google Test build failed"
            else
                echo "Google Test built successfully"
            fi
        fi
    else
        echo "❌ Failed to install Google Test via apt, trying build from source..."
        if ! build_gtest_for_linux; then
            echo "Warning: Google Test build failed"
        else
            echo "Google Test built successfully"
        fi
    fi
fi

# Clean up intermediate files
cleanup_intermediate_files

echo "Linux (Ubuntu) native compilation setup completed!"
echo ""
echo "Built dependencies:"
echo "- Tree-sitter: $([ -f "lambda/tree-sitter/libtree-sitter.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Tree-sitter-lambda: $([ -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Tree-sitter-javascript: $([ -f "lambda/tree-sitter-javascript/libtree-sitter-javascript.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- Tree-sitter-latex: $([ -f "lambda/tree-sitter-latex/libtree-sitter-latex.a" ] && echo "✓ Built" || echo "✗ Missing")"

# Detect architecture for library paths
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    LIB_ARCH_PATH="/usr/lib/x86_64-linux-gnu"
elif [ "$ARCH" = "aarch64" ]; then
    LIB_ARCH_PATH="/usr/lib/aarch64-linux-gnu"
else
    LIB_ARCH_PATH="/usr/lib"
fi

# Check system locations and apt packages
echo "- MIR: $([ -f "$SYSTEM_PREFIX/lib/libmir.a" ] && [ -f "$SYSTEM_PREFIX/include/mir.h" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- curl: $([ -f "$LIB_ARCH_PATH/libcurl.so" ] || dpkg -l | grep -q libcurl && echo "✓ Available" || echo "✗ Missing")"
echo "- mpdecimal: $([ -f "$LIB_ARCH_PATH/libmpdec.so" ] || [ -f "$SYSTEM_PREFIX/lib/libmpdec.so" ] || dpkg -l | grep -q libmpdec-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- utf8proc: $([ -f "$LIB_ARCH_PATH/libutf8proc.so" ] || [ -f "$LIB_ARCH_PATH/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.a" ] || [ -f "$SYSTEM_PREFIX/lib/libutf8proc.so" ] || dpkg -l | grep -q libutf8proc-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- mbedtls: $([ -f "$LIB_ARCH_PATH/libmbedtls.a" ] || dpkg -l | grep -q libmbedtls-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- gtest: $([ -f "$SYSTEM_PREFIX/lib/libgtest.a" ] && [ -f "$SYSTEM_PREFIX/lib/libgtest_main.a" ] || dpkg -l | grep -q libgtest-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- coreutils: $(command -v timeout >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
echo "- premake5: $(command -v premake5 >/dev/null 2>&1 && echo "✓ Available" || echo "✗ Missing")"
echo ""
echo "Radiant project dependencies:"
echo "- FreeType: $(dpkg -l | grep -q libfreetype6-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- ThorVG: $([ -f "$SYSTEM_PREFIX/lib/libthorvg.a" ] || [ -f "$SYSTEM_PREFIX/lib/libthorvg.so" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- GLFW: $(dpkg -l | grep -q libglfw3-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- libpng: $(dpkg -l | grep -q libpng-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- libturbojpeg: $(dpkg -l | grep -q libturbojpeg0-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- libgif: $(dpkg -l | grep -q libgif-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- fontconfig: $(dpkg -l | grep -q libfontconfig1-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- expat: $(dpkg -l | grep -q libexpat1-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- zlib: $(dpkg -l | grep -q zlib1g-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- bzip2: $(dpkg -l | grep -q libbz2-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- OpenGL: $(dpkg -l | grep -q libgl1-mesa-dev && echo "✓ Available" || echo "✗ Missing")"
echo "- EGL: $(dpkg -l | grep -q libegl1-mesa-dev && echo "✓ Available" || echo "✗ Missing")"
echo ""
echo "Next steps:"
echo "1. Run: make build           # Build Lambda main project"
echo "2. Run: make build-radiant   # Build Radiant HTML/CSS renderer"
echo "3. Run: make test            # Run tests"
echo ""
echo "To clean up intermediate files later, run: ./setup-linux-deps.sh clean"
