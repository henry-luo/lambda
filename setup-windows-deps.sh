#!/bin/bash

# Windows cross-compilation dependency setup script
set -e

DEPS_DIR="windows-deps"
TARGET_TRIPLET="x86_64-w64-mingw32"

echo "Setting up Windows cross-compilation dependencies..."

# Create dependencies directory
mkdir -p "$DEPS_DIR"/{include,lib,src}

# Function to download and extract if not exists
download_extract() {
    local name="$1"
    local url="$2"
    local archive="$3"
    
    if [ ! -d "$DEPS_DIR/src/$name" ]; then
        echo "Downloading $name..."
        cd "$DEPS_DIR/src"
        curl -L "$url" -o "$archive"
        
        case "$archive" in
            *.tar.gz) tar -xzf "$archive" ;;
            *.tar.bz2) tar -xjf "$archive" ;;
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
    
    echo "Building $name for Windows..."
    cd "$DEPS_DIR/src/$src_dir"
    
    export CC="$TARGET_TRIPLET-gcc"
    export CXX="$TARGET_TRIPLET-g++"
    export AR="$TARGET_TRIPLET-ar"
    export STRIP="$TARGET_TRIPLET-strip"
    export RANLIB="$TARGET_TRIPLET-ranlib"
    export CFLAGS="-O2 -static"
    export CXXFLAGS="-O2 -static"
    export LDFLAGS="-static"
    
    eval "$build_cmd"
    
    cd - > /dev/null
}

# Check for cross-compiler
if ! command -v "$TARGET_TRIPLET-gcc" >/dev/null 2>&1; then
    echo "Error: MinGW-w64 cross-compiler not found!"
    echo "Install it with: brew install mingw-w64"
    exit 1
fi

echo "Found cross-compiler: $TARGET_TRIPLET-gcc"

# Download and build Tree-sitter for Windows
if [ ! -f "$DEPS_DIR/lib/libtree-sitter.a" ]; then
    echo "Building tree-sitter for Windows..."
    cd lambda/tree-sitter
    
    # Clean previous builds
    make clean || true
    
    # Build static library for Windows
    CC="$TARGET_TRIPLET-gcc" \
    AR="$TARGET_TRIPLET-ar" \
    CFLAGS="-O3 -Wall -Wextra -Wshadow -Wpedantic -static" \
    make libtree-sitter.a
    
    # Copy to windows deps
    cp libtree-sitter.a "../../$DEPS_DIR/lib/libtree-sitter.a"
    mkdir -p "../../$DEPS_DIR/include"
    cp -r lib/include/* "../../$DEPS_DIR/include/"
    
    cd - > /dev/null
    echo "Tree-sitter built successfully"
else
    echo "Tree-sitter already built for Windows"
fi

# Build lexbor for Windows (if source is available)
if [ -d "lexbor-src" ] && [ ! -f "$DEPS_DIR/lib/liblexbor_static.a" ]; then
    build_dependency "lexbor" "lexbor" "
        mkdir -p build-windows
        cd build-windows
        cmake .. \
            -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DLEXBOR_BUILD_SHARED=OFF \
            -DLEXBOR_BUILD_STATIC=ON
        make -j\$(nproc)
        cp source/lexbor/liblexbor_static.a ../../../lib/
        cp -r ../source/lexbor ../../../include/
    "
fi

# Download and build GMP if not available
if [ ! -f "$DEPS_DIR/lib/libgmp.a" ]; then
    echo "Downloading and building GMP for Windows..."
    
    # Try multiple GMP download sources
    if [ ! -d "$DEPS_DIR/src/gmp-6.2.1" ]; then
        cd "$DEPS_DIR/src"
        
        # Try primary source
        if ! curl -L "https://gmplib.org/download/gmp/gmp-6.2.1.tar.bz2" -o "gmp-6.2.1.tar.bz2"; then
            # Try alternative mirror
            echo "Primary download failed, trying alternative source..."
            if ! curl -L "https://ftp.gnu.org/gnu/gmp/gmp-6.2.1.tar.bz2" -o "gmp-6.2.1.tar.bz2"; then
                echo "Warning: Could not download GMP source"
                cd - > /dev/null
                echo "Please manually download GMP from https://gmplib.org/download/gmp/"
            else
                tar -xjf "gmp-6.2.1.tar.bz2" && rm "gmp-6.2.1.tar.bz2"
                cd - > /dev/null
            fi
        else
            tar -xjf "gmp-6.2.1.tar.bz2" && rm "gmp-6.2.1.tar.bz2"
            cd - > /dev/null
        fi
    fi
    
    if [ -d "$DEPS_DIR/src/gmp-6.2.1" ]; then
        build_dependency "gmp" "gmp-6.2.1" "
            ./configure --host=$TARGET_TRIPLET \
                --prefix=\$(pwd)/../../../$DEPS_DIR \
                --enable-static \
                --disable-shared \
                CFLAGS=\"-O2 -static\"
            make -j\$(nproc || echo 4)
            make install
        "
        echo "GMP built successfully"
    else
        echo "Warning: GMP source not available, skipping build"
    fi
fi

# Try to download and build zlog if possible
if [ ! -f "$DEPS_DIR/lib/libzlog.a" ]; then
    echo "Attempting to build zlog for Windows..."
    if [ ! -d "$DEPS_DIR/src/zlog" ]; then
        cd "$DEPS_DIR/src"
        git clone https://github.com/HardySimpson/zlog.git || {
            echo "Warning: Could not clone zlog repository"
            cd - > /dev/null
        }
        cd - > /dev/null
    fi
    
    if [ -d "$DEPS_DIR/src/zlog" ]; then
        build_dependency "zlog" "zlog" "
            # zlog might need modifications for Windows
            CC=$TARGET_TRIPLET-gcc \
            AR=$TARGET_TRIPLET-ar \
            CFLAGS=\"-O2 -static -DWIN32\" \
            make PREFIX=\$(pwd)/../../../$DEPS_DIR static || {
                echo \"Warning: zlog build failed, may need manual setup\"
                exit 0
            }
            cp lib/libzlog.a ../../../lib/ || echo \"zlog library not found\"
            mkdir -p ../../../include/zlog
            cp src/*.h ../../../include/zlog/ || echo \"zlog headers not found\"
        "
    fi
fi

# Create a simple Makefile for remaining dependencies if they need to be built
cat > "$DEPS_DIR/build_deps.mk" << 'EOF'
# Makefile for building Windows dependencies
TARGET = x86_64-w64-mingw32
CC = $(TARGET)-gcc
CXX = $(TARGET)-g++
AR = $(TARGET)-ar
CFLAGS = -O2 -static -I../include
LDFLAGS = -static

# Example for building a simple static library
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libexample.a: example.o
	$(AR) rcs $@ $^

clean:
	rm -f *.o *.a
EOF

echo "Windows cross-compilation setup completed!"
echo "Next steps:"
echo "1. Build required dependencies manually"
echo "2. Run: ./compile-lambda-cross.sh build_lambda_windows_config.json"
