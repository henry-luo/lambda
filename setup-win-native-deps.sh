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
# Note: pkgconf is already installed as a dependency of cmake, no need for separate pkg-config

# Essential libraries for Lambda
echo ""
echo "Installing Lambda dependencies..."

# GMP (GNU Multiple Precision Arithmetic Library)
install_msys2_package "${TOOLCHAIN_PREFIX}-gmp" "GMP (GNU Multiple Precision Arithmetic Library)"

# ICU (Unicode support - optional, for advanced Unicode handling)
install_msys2_package "${TOOLCHAIN_PREFIX}-icu" "ICU (International Components for Unicode)"

# Development tools
install_msys2_package "git" "Git version control"
install_msys2_package "${TOOLCHAIN_PREFIX}-gdb" "GDB debugger"
install_msys2_package "vim" "Vim editor (includes xxd utility)"
install_msys2_package "jq" "JSON processor for build script configuration parsing"

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

# Skip tree-sitter builds - they are managed directly under lambda/
echo "⏭️  Skipping tree-sitter builds (managed directly under lambda/)"

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
            wget "$url" -O "$archive"
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
        
        # Use CLANG64 tools - only build static library to avoid linker issues
        CC="/clang64/bin/clang.exe" \
        AR="/clang64/bin/llvm-ar.exe" \
        CFLAGS="-O2 -DNDEBUG -fPIC" \
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
tools=("make" "cmake" "ninja" "git" "pkg-config")
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
if [[ "$MSYSTEM" == "CLANG64" ]]; then
    MSYS2_PREFIX="/clang64"
    MSYS2_LIB_PREFIX="mingw-w64-clang-x86_64"
elif [[ "$MSYSTEM" == "MINGW64" ]]; then
    MSYS2_PREFIX="/mingw64"
    MSYS2_LIB_PREFIX="mingw-w64-x86_64"
else
    MSYS2_PREFIX="/usr"
    MSYS2_LIB_PREFIX="unknown"
fi

cat > "build_lambda_win_native_config.json" << EOF
{
    "compiler": "${CC:-clang}",
    "output": "lambda-native.exe",
    "source_dirs": [],
    "source_files": [
        "lambda/input/input.cpp",
        "lambda/input/input-json.cpp",
        "lambda/input/input-ini.cpp",
        "lambda/input/input-xml.cpp",
        "lambda/input/input-yaml.cpp",
        "lambda/input/input-toml.cpp",
        "lambda/input/input-common.cpp",
        "lambda/input/input-css.cpp",
        "lambda/input/input-csv.cpp",
        "lambda/input/input-eml.cpp",
        "lambda/input/input-html.cpp",
        "lambda/input/input-ics.cpp",
        "lambda/input/input-latex.cpp",
        "lambda/input/input-man.cpp",
        "lambda/input/input-vcf.cpp",
        "lambda/input/input-mark.cpp",
        "lambda/input/input-org.cpp",
        "lambda/input/input-math.cpp",
        "lambda/input/input-rtf.cpp",
        "lambda/input/input-pdf.cpp",
        "lambda/input/input-markup.cpp",
        "lambda/input/mime-detect.c",
        "lambda/input/mime-types.c",
        "lambda/tree-sitter-lambda/src/parser.c",
        "lambda/parse.c",
        "lib/strbuf.c",
        "lib/strview.c",
        "lib/arraylist.c",
        "lib/file.c",
        "lib/hashmap.c",
        "lib/mem-pool/src/variable.c",
        "lib/mem-pool/src/buffer.c",
        "lib/mem-pool/src/utils.c",
        "lib/url.c",
        "lib/utf.c",
        "lib/num_stack.c",
        "lib/string.c",
        "lib/datetime.c",
        "lambda/runner.cpp",
        "lambda/transpile.cpp",
        "lambda/transpile-mir.cpp",
        "lambda/build_ast.cpp",
        "lambda/name_pool.cpp",
        "lambda/mir.c",
        "lambda/pack.cpp",
        "lambda/print.cpp",
        "lambda/format/format.cpp",
        "lambda/format/format-css.cpp",
        "lambda/format/format-latex.cpp",
        "lambda/format/format-html.cpp",
        "lambda/format/format-ini.cpp",
        "lambda/format/format-json.cpp",
        "lambda/format/format-math.cpp",
        "lambda/format/format-md.cpp",
        "lambda/format/format-org.cpp",
        "lambda/format/format-rst.cpp",
        "lambda/format/format-toml.cpp",
        "lambda/format/format-xml.cpp",
        "lambda/format/format-yaml.cpp",
        "lambda/lambda-eval.cpp",
        "lambda/utf_string.cpp",
        "lambda/lambda-mem.cpp",
        "lambda/validator/validate.cpp",
        "lambda/validator/validator.cpp",
        "lambda/validator/schema_parser.cpp",
        "lambda/main.cpp"
    ],
    "libraries": [
        {
            "name": "tree-sitter",
            "include": "lambda/tree-sitter/lib/include",
            "lib": "lambda/tree-sitter/libtree-sitter.a",
            "link": "static"
        },
        {
            "name": "tree-sitter-lambda",
            "include": "lambda/tree-sitter-lambda/bindings/c",
            "lib": "lambda/tree-sitter-lambda/libtree-sitter-lambda.a",
            "link": "static"
        },
        {
            "name": "mir",
            "include": "$DEPS_DIR/include",
            "lib": "$DEPS_DIR/lib/libmir.a",
            "link": "static"
        },
        {
            "name": "gmp",
            "include": "$MSYS2_PREFIX/include",
            "lib": "$MSYS2_PREFIX/lib",
            "link": "dynamic"
        }
    ],
    "warnings": [
        "format",
        "multichar"
    ],
    "flags": [
        "std=c++17",
        "fms-extensions",
        "pedantic",
        "fcolor-diagnostics",
        "fno-omit-frame-pointer",
        "g",
        "O2",
        "D_WIN32",
        "DWINDOWS",
        "D_GNU_SOURCE",
        "DNATIVE_WINDOWS_BUILD"
    ],
    "linker_flags": [
        "lgmp"
    ],
    "debug": false,
    "build_dir": "build_win_native",
    "cross_compile": false,
    "platforms": {
        "debug": {
            "output": "lambda-native-debug.exe",
            "flags": [
                "std=c++17",
                "fms-extensions",
                "pedantic",
                "fcolor-diagnostics",
                "fsanitize=address",
                "fno-omit-frame-pointer",
                "g",
                "O0",
                "D_WIN32",
                "DWINDOWS",
                "D_GNU_SOURCE",
                "DNATIVE_WINDOWS_BUILD",
                "DDEBUG"
            ],
            "linker_flags": [
                "fsanitize=address",
                "lgmp"
            ],
            "build_dir": "build_win_native_debug",
            "debug": true
        }
    }
}
EOF

echo "✅ Created build_lambda_win_native_config.json"

# Create convenience build script
cat > "compile-win-native.sh" << 'EOF'
#!/bin/bash

# Convenience script for Windows native compilation
# This script sets up the environment and calls the main compile script

# Check MSYS2 environment
if [[ "$MSYSTEM" != "MSYS" && "$MSYSTEM" != "MINGW64" && "$MSYSTEM" != "CLANG64" ]]; then
    echo "Warning: This script should be run in MSYS2 environment"
    echo "Current MSYSTEM: ${MSYSTEM:-not set}"
fi

# Set up compiler based on MSYS2 environment
if [[ "$MSYSTEM" == "CLANG64" ]]; then
    export CC="clang"
    export CXX="clang++"
    export AR="llvm-ar"
    export RANLIB="llvm-ranlib"
elif [[ "$MSYSTEM" == "MINGW64" ]]; then
    export CC="gcc"
    export CXX="g++"
    export AR="ar"
    export RANLIB="ranlib"
fi

# Set native Windows build environment
export NATIVE_WINDOWS_BUILD=1

echo "Windows Native Build Environment:"
echo "  MSYSTEM: ${MSYSTEM:-not set}"
echo "  CC: ${CC:-default}"
echo "  CXX: ${CXX:-default}"
echo ""

# Call main compile script with native Windows config
exec ./compile.sh build_lambda_win_native_config.json "$@"
EOF

chmod +x "compile-win-native.sh"
echo "✅ Created compile-win-native.sh convenience script"

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
echo "  ✅ Build tools (make, cmake, ninja)"
echo "  ✅ Compiler toolchain ($COMPILER_NAME)"
echo "  ✅ GMP (system package)"
echo "  ✅ ICU (Unicode support)"
echo "  ⏭️  Tree-sitter (managed directly under lambda/)"
echo "  📦 MIR (building from source - see verification below)"
echo ""

# Final verification
echo "Performing final verification..."

# Skip tree-sitter verification (managed under lambda/)
echo "⏭️  Tree-sitter libraries (managed directly under lambda/)"

# Check MIR
if [ -f "$DEPS_DIR/lib/libmir.a" ]; then
    echo "✅ MIR library available"
else
    echo "⚠️  MIR library missing"
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
