#!/bin/bash

# Windows cross-compilation dependency setup script
set -e

DEPS_DIR="windows-deps"
TARGET_TRIPLET="x86_64-w64-mingw32"

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
    rm -rf build_windows/ build/ build_debug/ 2>/dev/null || true
    
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
    
    if [ ! -d "$DEPS_DIR/src/$src_dir" ]; then
        echo "Warning: Source directory $DEPS_DIR/src/$src_dir not found"
        return 1
    fi
    
    cd "$DEPS_DIR/src/$src_dir"
    
    export CC="$TARGET_TRIPLET-gcc"
    export CXX="$TARGET_TRIPLET-g++"
    export AR="$TARGET_TRIPLET-ar"
    export STRIP="$TARGET_TRIPLET-strip"
    export RANLIB="$TARGET_TRIPLET-ranlib"
    # Use less aggressive flags for configure tests
    export CFLAGS="-O2"
    export CXXFLAGS="-O2"
    export LDFLAGS=""
    
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
    
    # Clean tree-sitter build files
    if [ -d "lambda/tree-sitter" ]; then
        cd lambda/tree-sitter
        make clean 2>/dev/null || true
        cd - > /dev/null
    fi
    
    # Clean build directories
    rm -rf build_windows/ build/ build_debug/ 2>/dev/null || true
    
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
}

# Function to build GMP stub as fallback
build_gmp_stub() {
    echo "Building GMP stub library..."
    
    # Create basic gmp.h stub
    mkdir -p "$DEPS_DIR/include"
    cat > "$DEPS_DIR/include/gmp.h" << 'EOF'
/* Minimal GMP stub for cross-compilation testing */
#ifndef __GMP_H__
#define __GMP_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int _mp_alloc;
    int _mp_size;
    unsigned long *_mp_d;
} __mpz_struct;

typedef __mpz_struct mpz_t[1];

typedef struct {
    int _mp_prec;
    int _mp_size;
    long _mp_exp;
    unsigned long *_mp_d;
} __mpf_struct;

typedef __mpf_struct mpf_t[1];

/* Basic function declarations */
void mpz_init(mpz_t x);
void mpz_clear(mpz_t x);
void mpz_set_str(mpz_t rop, const char *str, int base);
char* mpz_get_str(char *str, int base, const mpz_t op);
void mpz_add(mpz_t rop, const mpz_t op1, const mpz_t op2);
void mpz_sub(mpz_t rop, const mpz_t op1, const mpz_t op2);
void mpz_mul(mpz_t rop, const mpz_t op1, const mpz_t op2);

void mpf_init(mpf_t x);
void mpf_clear(mpf_t x);
void mpf_set_str(mpf_t rop, const char *str, int base);

#ifdef __cplusplus
}
#endif

#endif /* __GMP_H__ */
EOF

    # Create stub implementation
    cat > "$DEPS_DIR/src/gmp_stub.c" << 'EOF'
/* Minimal GMP stub implementation */
#include "../include/gmp.h"
#include <stdlib.h>
#include <string.h>

void mpz_init(mpz_t x) {
    x->_mp_alloc = 0;
    x->_mp_size = 0;
    x->_mp_d = NULL;
}

void mpz_clear(mpz_t x) {
    if (x->_mp_d) free(x->_mp_d);
    x->_mp_alloc = 0;
    x->_mp_size = 0;
    x->_mp_d = NULL;
}

void mpz_set_str(mpz_t rop, const char *str, int base) {
    // Stub implementation
    (void)rop; (void)str; (void)base;
}

char* mpz_get_str(char *str, int base, const mpz_t op) {
    // Stub implementation
    (void)base; (void)op;
    if (!str) str = malloc(32);
    strcpy(str, "0");
    return str;
}

void mpz_add(mpz_t rop, const mpz_t op1, const mpz_t op2) {
    // Stub implementation
    (void)rop; (void)op1; (void)op2;
}

void mpz_sub(mpz_t rop, const mpz_t op1, const mpz_t op2) {
    // Stub implementation  
    (void)rop; (void)op1; (void)op2;
}

void mpz_mul(mpz_t rop, const mpz_t op1, const mpz_t op2) {
    // Stub implementation
    (void)rop; (void)op1; (void)op2;
}

void mpf_init(mpf_t x) {
    x->_mp_prec = 0;
    x->_mp_size = 0;
    x->_mp_exp = 0;
    x->_mp_d = NULL;
}

void mpf_clear(mpf_t x) {
    if (x->_mp_d) free(x->_mp_d);
    x->_mp_prec = 0;
    x->_mp_size = 0;
    x->_mp_exp = 0;
    x->_mp_d = NULL;
}

void mpf_set_str(mpf_t rop, const char *str, int base) {
    // Stub implementation
    (void)rop; (void)str; (void)base;
}
EOF

    # Compile stub library
    cd "$DEPS_DIR/src"
    $TARGET_TRIPLET-gcc -I../include -c gmp_stub.c -o gmp_stub.o
    $TARGET_TRIPLET-ar rcs ../lib/libgmp.a gmp_stub.o
    cd - > /dev/null
    
    echo "GMP stub library created (for compilation testing only)"
}

# Function to build MIR for Windows
build_mir_for_windows() {
    echo "Building MIR for Windows..."
    
    if [ ! -d "$DEPS_DIR/src/mir" ]; then
        cd "$DEPS_DIR/src"
        echo "Cloning MIR repository..."
        git clone https://github.com/vnmakarov/mir.git || {
            echo "Warning: Could not clone MIR repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "$DEPS_DIR/src/mir"
    
    # Clean previous builds
    make clean 2>/dev/null || true
    
    # Create MIR cross-compilation configuration
    export CC="$TARGET_TRIPLET-gcc"
    export CXX="$TARGET_TRIPLET-g++"
    export AR="$TARGET_TRIPLET-ar"
    export RANLIB="$TARGET_TRIPLET-ranlib"
    export STRIP="$TARGET_TRIPLET-strip"
    
    # MIR-specific cross-compilation flags
    export CFLAGS="-O2 -DNDEBUG -static -fPIC"
    export CXXFLAGS="-O2 -DNDEBUG -static -fPIC"
    export LDFLAGS="-static"
    
    # Build MIR static library for Windows
    echo "Building MIR with cross-compiler..."
    
    # Check if MIR has a configure script
    if [ -f "configure" ]; then
        ./configure --host=$TARGET_TRIPLET \
            --enable-static \
            --disable-shared \
            --prefix="$(pwd)/../../../$DEPS_DIR"
        make -j$(nproc 2>/dev/null || echo 4)
        make install
    elif [ -f "Makefile" ]; then
        # Direct make approach for MIR
        make CC="$TARGET_TRIPLET-gcc" \
             AR="$TARGET_TRIPLET-ar" \
             CFLAGS="$CFLAGS" \
             -j$(nproc 2>/dev/null || echo 4)
        
        # Copy built libraries and headers
        if [ -f "libmir.a" ]; then
            cp libmir.a "../../../$DEPS_DIR/lib/"
        fi
        
        # Copy headers
        mkdir -p "../../../$DEPS_DIR/include"
        for header in mir.h mir-gen.h mir-varr.h mir-dlist.h mir-hash.h mir-htab.h; do
            if [ -f "$header" ]; then
                cp "$header" "../../../$DEPS_DIR/include/"
            fi
        done
    else
        # Build manually if no build system
        echo "Building MIR manually..."
        
        # Compile MIR source files
        MIR_SOURCES="mir.c mir-gen.c"
        MIR_OBJECTS=""
        
        for src in $MIR_SOURCES; do
            if [ -f "$src" ]; then
                obj="${src%.c}.o"
                echo "Compiling $src..."
                $TARGET_TRIPLET-gcc $CFLAGS -c "$src" -o "$obj"
                MIR_OBJECTS="$MIR_OBJECTS $obj"
            fi
        done
        
        # Create static library
        if [ -n "$MIR_OBJECTS" ]; then
            echo "Creating libmir.a..."
            $TARGET_TRIPLET-ar rcs libmir.a $MIR_OBJECTS
            cp libmir.a "../../../$DEPS_DIR/lib/"
            
            # Copy headers
            mkdir -p "../../../$DEPS_DIR/include"
            for header in mir.h mir-gen.h mir-varr.h mir-dlist.h mir-hash.h mir-htab.h; do
                if [ -f "$header" ]; then
                    cp "$header" "../../../$DEPS_DIR/include/"
                fi
            done
        else
            echo "Warning: No MIR source files found"
            cd - > /dev/null
            return 1
        fi
    fi
    
    cd - > /dev/null
    echo "MIR built successfully for Windows"
    return 0
}

# Function to create MIR stub as fallback
build_mir_stub() {
    echo "Creating MIR stub library..."
    
    # Create basic MIR headers
    mkdir -p "$DEPS_DIR/include"
    
    # Create mir.h stub
    cat > "$DEPS_DIR/include/mir.h" << 'EOF'
/* Minimal MIR stub header for cross-compilation testing */
#ifndef MIR_H
#define MIR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic MIR types */
typedef struct MIR_context *MIR_context_t;
typedef struct MIR_module *MIR_module_t;
typedef struct MIR_func *MIR_func_t;
typedef struct MIR_item *MIR_item_t;
typedef struct MIR_insn *MIR_insn_t;
typedef struct MIR_op *MIR_op_t;
typedef struct MIR_reg *MIR_reg_t;
typedef struct MIR_val *MIR_val_t;

typedef enum {
    MIR_T_I8, MIR_T_U8, MIR_T_I16, MIR_T_U16,
    MIR_T_I32, MIR_T_U32, MIR_T_I64, MIR_T_U64,
    MIR_T_F, MIR_T_D, MIR_T_LD, MIR_T_P,
    MIR_T_BLK, MIR_T_RBLK, MIR_T_BOUND
} MIR_type_t;

/* Basic function declarations */
void MIR_init(void);
void MIR_finish(void);
MIR_context_t MIR_init_context(void);
void MIR_finish_context(MIR_context_t ctx);
MIR_module_t MIR_new_module(MIR_context_t ctx, const char *name);
void MIR_finish_module(MIR_context_t ctx);

#ifdef __cplusplus
}
#endif

#endif /* MIR_H */
EOF

    # Create mir-gen.h stub
    cat > "$DEPS_DIR/include/mir-gen.h" << 'EOF'
/* Minimal MIR-GEN stub header for cross-compilation testing */
#ifndef MIR_GEN_H
#define MIR_GEN_H

#include "mir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Code generation context */
typedef struct MIR_gen_ctx *MIR_gen_ctx_t;

/* Basic generation functions */
void MIR_gen_init(MIR_context_t ctx);
void MIR_gen_finish(MIR_context_t ctx);
void *MIR_gen(MIR_context_t ctx, MIR_item_t func_item);

#ifdef __cplusplus
}
#endif

#endif /* MIR_GEN_H */
EOF

    # Create stub implementation
    cat > "$DEPS_DIR/src/mir_stub.c" << 'EOF'
/* Minimal MIR stub implementation */
#include "../include/mir.h"
#include "../include/mir-gen.h"
#include <stdio.h>
#include <stdlib.h>

void MIR_init(void) {
    /* Stub implementation */
}

void MIR_finish(void) {
    /* Stub implementation */
}

MIR_context_t MIR_init_context(void) {
    /* Stub implementation */
    return (MIR_context_t)malloc(1);
}

void MIR_finish_context(MIR_context_t ctx) {
    /* Stub implementation */
    if (ctx) free(ctx);
}

MIR_module_t MIR_new_module(MIR_context_t ctx, const char *name) {
    /* Stub implementation */
    (void)ctx; (void)name;
    return (MIR_module_t)malloc(1);
}

void MIR_finish_module(MIR_context_t ctx) {
    /* Stub implementation */
    (void)ctx;
}

void MIR_gen_init(MIR_context_t ctx) {
    /* Stub implementation */
    (void)ctx;
}

void MIR_gen_finish(MIR_context_t ctx) {
    /* Stub implementation */
    (void)ctx;
}

void *MIR_gen(MIR_context_t ctx, MIR_item_t func_item) {
    /* Stub implementation */
    (void)ctx; (void)func_item;
    return malloc(1);
}
EOF

    # Compile stub library
    cd "$DEPS_DIR/src"
    $TARGET_TRIPLET-gcc -I../include -c mir_stub.c -o mir_stub.o
    $TARGET_TRIPLET-ar rcs ../lib/libmir.a mir_stub.o
    cd - > /dev/null
    
    echo "MIR stub library created (for compilation testing only)"
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
                echo "For now, using stub GMP library..."
                # Fall back to stub
                build_gmp_stub
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
        # Try building real GMP, but fall back to stub if it fails
        if ! build_dependency "gmp" "gmp-6.2.1" "
            # Clean any previous build attempts
            make distclean 2>/dev/null || true
            
            # Try with minimal configuration that avoids problematic tests
            ./configure --host=$TARGET_TRIPLET \
                --prefix=\$(pwd)/../../../$DEPS_DIR \
                --enable-static \
                --disable-shared \
                --disable-assembly \
                --disable-fast-install \
                --enable-cxx=no \
                CFLAGS=\"-O1 -fno-stack-protector\" \
                CPPFLAGS=\"-DNDEBUG\" \
                ABI=longlong
            make -j\$(nproc || echo 4)
            make install
        "; then
            echo "Warning: GMP build failed, falling back to stub implementation"
            build_gmp_stub
        else
            echo "GMP built successfully"
        fi
    else
        echo "Warning: GMP source not available, using stub implementation"
        build_gmp_stub
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
            make PREFIX=\$(pwd)/../../../$DEPS_DIR all || {
                echo \"Warning: zlog build failed, may need manual setup\"
                return 1
            }
            # Copy any built libraries
            find . -name \"*.a\" -exec cp {} ../../../lib/ \\; 2>/dev/null || true
            # Copy headers
            mkdir -p ../../../include/zlog
            find . -name \"*.h\" -exec cp {} ../../../include/zlog/ \\; 2>/dev/null || true
        " || echo "zlog build failed but continuing..."
    fi
fi

# Build MIR for Windows
if [ ! -f "$DEPS_DIR/lib/libmir.a" ]; then
    echo "Building MIR for Windows..."
    
    if ! build_mir_for_windows; then
        echo "Warning: MIR build failed, falling back to stub implementation"
        build_mir_stub
    else
        echo "MIR built successfully"
    fi
else
    echo "MIR already built for Windows"
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

# Clean up intermediate files
cleanup_intermediate_files

echo "Windows cross-compilation setup completed!"
echo ""
echo "Built dependencies:"
echo "- Tree-sitter: $([ -f "$DEPS_DIR/lib/libtree-sitter.a" ] && echo "✓ Built" || echo "✗ Missing")"
echo "- GMP: $([ -f "$DEPS_DIR/lib/libgmp.a" ] && echo "✓ Built ($([ -f "$DEPS_DIR/src/gmp_stub.c" ] && echo "stub" || echo "real"))" || echo "✗ Missing")"
echo "- MIR: $([ -f "$DEPS_DIR/lib/libmir.a" ] && echo "✓ Built ($([ -f "$DEPS_DIR/src/mir_stub.c" ] && echo "stub" || echo "real"))" || echo "✗ Missing")"
echo "- zlog: $([ -f "$DEPS_DIR/lib/libzlog.a" ] && echo "✓ Built" || echo "✗ Missing (optional)")"
echo ""
echo "Next steps:"
echo "1. Build required dependencies manually (see windows-deps/README.md)"
echo "2. Run: ./compile-lambda-cross.sh build_lambda_windows_config.json"
echo ""
echo "To clean up intermediate files later, run: ./setup-windows-deps.sh clean"
