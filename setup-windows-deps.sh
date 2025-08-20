#!/bin/bash

# Windows cross-compilation dependency setup script
set -e

SCRIPT_DIR="$(pwd)"
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
        
        # Clean up lexbor-specific build files
        if [ -d "$DEPS_DIR/src/lexbor" ]; then
            cd "$DEPS_DIR/src/lexbor"
            rm -rf build-windows CMakeCache.txt CMakeFiles/ cmake_install.cmake 2>/dev/null || true
            find . -name "*.cmake" -type f ! -path "./cmake/*" -delete 2>/dev/null || true
            cd - > /dev/null
        fi
            make clean 2>/dev/null || true
            rm -f *.o *.a src/*.o 2>/dev/null || true
            cd - > /dev/null
        fi
        
        # Clean up MIR-specific build files
        if [ -d "$DEPS_DIR/src/mir" ]; then
            cd "$DEPS_DIR/src/mir"
            make clean 2>/dev/null || true
            rm -f *.o *.a 2>/dev/null || true
            cd - > /dev/null
        fi
        
        # Clean up GMP build files
        if [ -d "$DEPS_DIR/src/gmp-6.2.1" ]; then
            cd "$DEPS_DIR/src/gmp-6.2.1"
            make distclean 2>/dev/null || true
            cd - > /dev/null
        fi
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
        
        # Clean lexbor CMake files
        if [ -d "$DEPS_DIR/src/lexbor" ]; then
            rm -rf "$DEPS_DIR/src/lexbor/build-windows" 2>/dev/null || true
            rm -f "$DEPS_DIR/src/lexbor/CMakeCache.txt" 2>/dev/null || true
            find "$DEPS_DIR/src/lexbor" -name "*.cmake" -type f ! -path "./cmake/*" -delete 2>/dev/null || true
        fi
        
        # Clean MIR build files
        if [ -d "$DEPS_DIR/src/mir" ]; then
            cd "$DEPS_DIR/src/mir"
            make clean 2>/dev/null || true
            rm -f *.o *.a 2>/dev/null || true
            cd - > /dev/null
        fi
        
        # Clean GMP build files
        if [ -d "$DEPS_DIR/src/gmp-6.2.1" ]; then
            cd "$DEPS_DIR/src/gmp-6.2.1"
            make distclean 2>/dev/null || true
            cd - > /dev/null
        fi
    fi
    
    echo "Cleanup completed."
}

# Function to build full GMP library for Windows
build_gmp_full() {
    echo "Building full GMP library for Windows..."
    
    # Try using MPIR (Windows-friendly GMP fork) first, then GMP
    MPIR_VERSION="3.0.0"
    MPIR_DIR="mpir-${MPIR_VERSION}"
    MPIR_ARCHIVE="${MPIR_DIR}.tar.bz2"
    MPIR_URL="http://mpir.org/${MPIR_ARCHIVE}"
    
    GMP_VERSION="6.2.1"  # Use older, more stable version
    GMP_DIR="gmp-${GMP_VERSION}"
    GMP_ARCHIVE="${GMP_DIR}.tar.xz"
    GMP_URL="https://gmplib.org/download/gmp/${GMP_ARCHIVE}"
    
    mkdir -p "$DEPS_DIR/src"
    cd "$DEPS_DIR/src"
    
    # Try MPIR first (Windows-friendly)
    if [ ! -f "$MPIR_ARCHIVE" ] && [ ! -d "$MPIR_DIR" ]; then
        echo "Trying MPIR (Windows-friendly GMP fork)..."
        if command -v curl >/dev/null 2>&1; then
            if curl -L --fail --connect-timeout 10 -o "$MPIR_ARCHIVE" "$MPIR_URL" 2>/dev/null; then
                echo "✅ Downloaded MPIR successfully"
                if tar -xjf "$MPIR_ARCHIVE" 2>/dev/null; then
                    echo "✅ Extracted MPIR successfully"
                    if build_mpir_windows "$MPIR_DIR"; then
                        cd "$SCRIPT_DIR"
                        return 0
                    fi
                fi
            fi
        fi
        echo "MPIR download/build failed, trying GMP..."
    fi
    
    # Fall back to GMP
    if [ ! -f "$GMP_ARCHIVE" ]; then
        echo "Downloading GMP $GMP_VERSION..."
        
        # Try curl first
        if command -v curl >/dev/null 2>&1; then
            echo "Using curl to download..."
            if curl -L --fail --retry 3 --retry-delay 5 -o "$GMP_ARCHIVE" "$GMP_URL"; then
                echo "✅ Downloaded successfully with curl"
            else
                echo "❌ Curl download failed, trying wget..."
                rm -f "$GMP_ARCHIVE"  # Remove partial file
                
                if command -v wget >/dev/null 2>&1; then
                    if wget --tries=3 --timeout=30 -O "$GMP_ARCHIVE" "$GMP_URL"; then
                        echo "✅ Downloaded successfully with wget"
                    else
                        echo "❌ All download attempts failed"
                        rm -f "$GMP_ARCHIVE"  # Remove partial file
                        echo "Please manually download GMP from $GMP_URL and place it in $DEPS_DIR/src/"
                        return 1
                    fi
                else
                    echo "❌ Neither curl nor wget available for download"
                    echo "Please install curl or wget, or manually download GMP from:"
                    echo "  $GMP_URL"
                    echo "Place the file in: $DEPS_DIR/src/$GMP_ARCHIVE"
                    return 1
                fi
            fi
        elif command -v wget >/dev/null 2>&1; then
            echo "Using wget to download..."
            if wget --tries=3 --timeout=30 -O "$GMP_ARCHIVE" "$GMP_URL"; then
                echo "✅ Downloaded successfully with wget"
            else
                echo "❌ Wget download failed"
                rm -f "$GMP_ARCHIVE"  # Remove partial file
                echo "Please manually download GMP from $GMP_URL and place it in $DEPS_DIR/src/"
                return 1
            fi
        else
            echo "❌ Neither curl nor wget available"
            echo "Please install curl or wget, or manually download GMP from:"
            echo "  $GMP_URL"
            echo "Place the file in: $DEPS_DIR/src/$GMP_ARCHIVE"
            return 1
        fi
    else
        echo "✅ GMP archive already exists: $GMP_ARCHIVE"
    fi

    # Extract GMP
    if [ ! -d "$GMP_DIR" ]; then
        echo "Extracting GMP..."
        # Try different extraction methods based on file extension
        case "$GMP_ARCHIVE" in
            *.tar.xz)
                if command -v xz >/dev/null 2>&1; then
                    xz -dc "$GMP_ARCHIVE" | tar -xf - || {
                        echo "Failed to extract with xz, trying tar directly..."
                        tar -xf "$GMP_ARCHIVE" || {
                            echo "Failed to extract GMP archive"
                            return 1
                        }
                    }
                else
                    tar -xf "$GMP_ARCHIVE" || {
                        echo "Failed to extract GMP archive (xz not available)"
                        return 1
                    }
                fi
                ;;
            *.tar.bz2)
                tar -xjf "$GMP_ARCHIVE" || {
                    echo "Failed to extract GMP archive"
                    return 1
                }
                ;;
            *.tar.gz)
                tar -xzf "$GMP_ARCHIVE" || {
                    echo "Failed to extract GMP archive"
                    return 1
                }
                ;;
            *)
                tar -xf "$GMP_ARCHIVE" || {
                    echo "Failed to extract GMP archive"
                    return 1
                }
                ;;
        esac
        echo "✅ Extraction completed"
    else
        echo "✅ GMP source directory already exists: $GMP_DIR"
    fi
    
    # Try building GMP with simpler configuration
    if build_gmp_simple_windows "$GMP_DIR"; then
        cd "$SCRIPT_DIR"
        return 0
    fi
    
    cd "$SCRIPT_DIR"
    return 1
}

# Function to build MPIR for Windows
build_mpir_windows() {
    local MPIR_DIR="$1"
    echo "Building MPIR for Windows..."
    
    cd "$MPIR_DIR" || return 1
    
    # Clean previous builds
    make distclean 2>/dev/null || true
    
    # Set cross-compilation environment
    export CC="$TARGET_TRIPLET-gcc"
    export AR="$TARGET_TRIPLET-ar"
    export RANLIB="$TARGET_TRIPLET-ranlib"
    export STRIP="$TARGET_TRIPLET-strip"
    
    # MPIR has better Windows support
    export CFLAGS="-O2"
    export LDFLAGS=""
    
    # Configure MPIR
    if ./configure \
        --host=$TARGET_TRIPLET \
        --enable-static \
        --disable-shared \
        --prefix="$(pwd)/../../../../$DEPS_DIR" \
        --enable-gmpcompat; then
        
        echo "Building MPIR..."
        if make -j$(nproc 2>/dev/null || echo 4); then
            echo "Installing MPIR..."
            if make install; then
                echo "✅ MPIR built and installed successfully"
                return 0
            fi
        fi
    fi
    
    echo "❌ MPIR build failed"
    return 1
}

# Function to build GMP with simple configuration
build_gmp_simple_windows() {
    local GMP_DIR="$1"
    echo "Building GMP with simple configuration..."
    
    cd "$GMP_DIR" || return 1
    
    # Clean previous builds
    make distclean 2>/dev/null || true
    
    # Very simple cross-compilation setup
    export CC="$TARGET_TRIPLET-gcc"
    export AR="$TARGET_TRIPLET-ar"
    export RANLIB="$TARGET_TRIPLET-ranlib"
    
    # Minimal flags to avoid test failures
    export CFLAGS="-O1"
    export LDFLAGS=""
    
    # Try minimal configure - avoid problematic options
    if ./configure \
        --host=$TARGET_TRIPLET \
        --disable-assembly \
        --disable-shared \
        --enable-static \
        --disable-cxx; then
        
        echo "Building GMP..."
        if make -j2; then  # Use fewer parallel jobs
            echo "Installing GMP..."
            # Manual install to avoid test failures
            mkdir -p "../../../../$DEPS_DIR/lib"
            mkdir -p "../../../../$DEPS_DIR/include"
            
            if [ -f ".libs/libgmp.a" ]; then
                cp ".libs/libgmp.a" "../../../../$DEPS_DIR/lib/"
            elif [ -f "libgmp.a" ]; then
                cp "libgmp.a" "../../../../$DEPS_DIR/lib/"
            else
                echo "❌ Could not find built GMP library"
                return 1
            fi
            
            # Copy headers
            if [ -f "gmp.h" ]; then
                cp "gmp.h" "../../../../$DEPS_DIR/include/"
            fi
            
            # Verify the build
            if [ -f "../../../../$DEPS_DIR/lib/libgmp.a" ]; then
                LIB_SIZE=$(stat -f%z "../../../../$DEPS_DIR/lib/libgmp.a" 2>/dev/null || stat -c%s "../../../../$DEPS_DIR/lib/libgmp.a" 2>/dev/null)
                echo "✅ GMP library built successfully: ${LIB_SIZE} bytes"
                
                # Check if gmp_sprintf is available
                if nm "../../../../$DEPS_DIR/lib/libgmp.a" 2>/dev/null | grep -q gmp_sprintf; then
                    echo "✅ Full GMP with I/O functions (including gmp_sprintf) built successfully!"
                else
                    echo "⚠️  GMP built but I/O functions may not be available"
                fi
                return 0
            fi
        fi
    fi
    
    echo "❌ Simple GMP build failed"
    return 1
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
    mkdir -p "$DEPS_DIR/src"
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
    mkdir -p "../lib"  # Ensure lib directory exists
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
        for header in mir.h mir-gen.h mir-varr.h mir-dlist.h mir-hash.h mir-htab.h mir-alloc.h mir-bitmap.h mir-code-alloc.h; do
            if [ -f "$header" ]; then
                cp "$header" "../../../$DEPS_DIR/include/"
            fi
        done
        
        # Copy c2mir header
        if [ -f "c2mir/c2mir.h" ]; then
            cp "c2mir/c2mir.h" "../../../$DEPS_DIR/include/"
        fi
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
            for header in mir.h mir-gen.h mir-varr.h mir-dlist.h mir-hash.h mir-htab.h mir-alloc.h mir-bitmap.h mir-code-alloc.h; do
                if [ -f "$header" ]; then
                    cp "$header" "../../../$DEPS_DIR/include/"
                fi
            done
            
            # Copy c2mir header
            if [ -f "c2mir/c2mir.h" ]; then
                cp "c2mir/c2mir.h" "../../../$DEPS_DIR/include/"
            fi
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

# Function to build lexbor for Windows
build_lexbor_for_windows() {
    echo "Building lexbor for Windows..."
    
    # Ensure lexbor source is available
    if [ ! -d "$DEPS_DIR/src/lexbor" ]; then
        cd "$DEPS_DIR/src"
        echo "Cloning lexbor repository..."
        git clone https://github.com/lexbor/lexbor.git || {
            echo "Warning: Could not clone lexbor repository"
            cd - > /dev/null
            return 1
        }
        cd - > /dev/null
    fi
    
    cd "$DEPS_DIR/src/lexbor"
    
    # Clean previous builds thoroughly
    echo "Cleaning previous builds..."
    rm -rf build-windows CMakeCache.txt CMakeFiles/ cmake_install.cmake install_manifest.txt 2>/dev/null || true
    find . -name "*.cmake" -type f ! -path "./cmake/*" -delete 2>/dev/null || true
    
    # Verify we have CMakeLists.txt
    if [ ! -f "CMakeLists.txt" ]; then
        echo "Warning: CMakeLists.txt not found in lexbor directory"
        cd - > /dev/null
        return 1
    fi
    
    # Create missing config.cmake if it doesn't exist
    if [ ! -f "config.cmake" ] || [ ! -s "config.cmake" ]; then
        echo "Creating missing config.cmake..."
        
        # Read version from version file
        if [ -f "version" ]; then
            VERSION_LINE=$(cat version)
            VERSION=$(echo "$VERSION_LINE" | sed 's/LEXBOR_VERSION=//')
            MAJOR=$(echo "$VERSION" | cut -d. -f1)
            MINOR=$(echo "$VERSION" | cut -d. -f2)
            PATCH=$(echo "$VERSION" | cut -d. -f3)
        else
            # Default version if file not found
            VERSION="2.5.0"
            MAJOR="2"
            MINOR="5"
            PATCH="0"
        fi
        
        cat > config.cmake << EOF
# Lexbor configuration
function(GET_LEXBOR_VERSION MAJOR MINOR PATCH VERSION_STRING)
    set(\${MAJOR} "$MAJOR" PARENT_SCOPE)
    set(\${MINOR} "$MINOR" PARENT_SCOPE)
    set(\${PATCH} "$PATCH" PARENT_SCOPE)
    set(\${VERSION_STRING} "$VERSION" PARENT_SCOPE)
endfunction()
EOF
        echo "Created config.cmake with version $VERSION"
    fi
    
    # Create missing feature.cmake if it doesn't exist
    if [ ! -f "feature.cmake" ] || [ ! -s "feature.cmake" ]; then
        echo "Creating missing feature.cmake..."
        
        cat > feature.cmake << 'EOF'
# Lexbor feature configuration
function(GET_MODULES_LIST MODULES_VAR SOURCE_DIR)
    file(GLOB_RECURSE MODULE_FILES "${SOURCE_DIR}/*/*/CMakeLists.txt")
    set(MODULES "")
    foreach(MODULE_FILE ${MODULE_FILES})
        get_filename_component(MODULE_DIR ${MODULE_FILE} DIRECTORY)
        get_filename_component(MODULE_NAME ${MODULE_DIR} NAME)
        list(APPEND MODULES ${MODULE_NAME})
    endforeach()
    list(REMOVE_DUPLICATES MODULES)
    set(${MODULES_VAR} ${MODULES} PARENT_SCOPE)
endfunction()
EOF
        echo "Created feature.cmake"
    fi
    
    # Create CMake toolchain file for cross-compilation
    mkdir -p cmake
    cat > cmake/mingw-w64-x86_64.cmake << 'EOF'
# CMake toolchain file for MinGW-w64 cross-compilation

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Cross-compiler tools
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Target environment
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Search settings
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Compiler flags
set(CMAKE_C_FLAGS "-static -O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS "-static -O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(CMAKE_SHARED_LINKER_FLAGS "-static")
set(CMAKE_MODULE_LINKER_FLAGS "-static")
EOF

    # Create build directory and configure
    mkdir -p build-windows
    cd build-windows
    
    echo "Configuring lexbor with CMake..."
    
    # Configure with CMake
    if cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DLEXBOR_BUILD_SHARED=OFF \
        -DLEXBOR_BUILD_STATIC=ON \
        -DLEXBOR_BUILD_TESTS=OFF \
        -DLEXBOR_BUILD_EXAMPLES=OFF \
        -DLEXBOR_BUILD_UTILS=OFF \
        -DLEXBOR_WITHOUT_THREADS=ON \
        -DCMAKE_INSTALL_PREFIX="$(pwd)/../../../../$DEPS_DIR" \
        -DCMAKE_VERBOSE_MAKEFILE=OFF; then
        
        echo "CMake configuration successful, building..."
        
        # Build the library with specific number of jobs
        NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        if make -j"$NPROC" VERBOSE=0; then
            echo "lexbor build successful, installing..."
            
            # Try installing to our deps directory
            if make install; then
                echo "lexbor installed successfully"
            else
                echo "Install failed, copying manually..."
                
                # Manual copy of libraries and headers
                echo "Looking for built libraries..."
                find . -name "*.a" -print0 | while IFS= read -r -d '' lib; do
                    echo "Found library: $lib"
                    cp "$lib" "../../../../$DEPS_DIR/lib/" && echo "Copied: $(basename "$lib")"
                done
                
                # Copy headers manually
                if [ -d "../source" ]; then
                    echo "Copying headers..."
                    mkdir -p "../../../../$DEPS_DIR/include"
                    cp -r ../source/lexbor "../../../../$DEPS_DIR/include/" 2>/dev/null || {
                        echo "Warning: Could not copy all headers"
                    }
                fi
            fi
            
            # Verify that we got the library we need
            if [ -f "../../../../$DEPS_DIR/lib/liblexbor_static.a" ]; then
                echo "✓ Found liblexbor_static.a"
            else
                # Look for alternative library names
                echo "Looking for alternative library names..."
                ls -la "../../../../$DEPS_DIR/lib/" | grep -i lexbor || true
                if ls "../../../../$DEPS_DIR/lib/"*lexbor* 1> /dev/null 2>&1; then
                    echo "Found lexbor libraries with different names"
                fi
            fi
            
            cd - > /dev/null
            echo "lexbor built successfully for Windows"
            return 0
        else
            echo "Warning: lexbor build failed"
            cd - > /dev/null
            return 1
        fi
    else
        echo "Warning: CMake configuration failed for lexbor"
        cd - > /dev/null
        return 1
    fi
}

# Function to create lexbor stub as fallback
build_lexbor_stub() {
    echo "Creating lexbor stub library..."
    
    # Get back to the script's original directory if we're somewhere else
    if [ ! -d "$DEPS_DIR" ]; then
        # We might be in a subdirectory, find where the deps dir should be
        while [ ! -d "$DEPS_DIR" ] && [ "$(pwd)" != "/" ]; do
            cd ..
        done
        if [ ! -d "$DEPS_DIR" ]; then
            echo "Error: Cannot find $DEPS_DIR directory"
            return 1
        fi
    fi
    
    # Create basic lexbor headers
    mkdir -p "$DEPS_DIR/include/lexbor"
    
    # Create lexbor/core/core.h stub
    mkdir -p "$DEPS_DIR/include/lexbor/core"
    cat > "$DEPS_DIR/include/lexbor/core/core.h" << 'EOF'
/* Minimal lexbor core stub header for cross-compilation testing */
#ifndef LEXBOR_CORE_H
#define LEXBOR_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic types */
typedef unsigned char lxb_char_t;
typedef uint32_t lxb_codepoint_t;
typedef int lxb_status_t;

/* Status codes */
#define LXB_STATUS_OK 0x00
#define LXB_STATUS_ERROR 0x01

/* Basic function declarations */
lxb_status_t lexbor_init(void);
void lexbor_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_CORE_H */
EOF

    # Create lexbor/html/html.h stub
    mkdir -p "$DEPS_DIR/include/lexbor/html"
    cat > "$DEPS_DIR/include/lexbor/html/html.h" << 'EOF'
/* Minimal lexbor HTML stub header for cross-compilation testing */
#ifndef LEXBOR_HTML_H
#define LEXBOR_HTML_H

#include "../core/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Basic HTML types */
typedef struct lxb_html_document lxb_html_document_t;
typedef struct lxb_html_element lxb_html_element_t;

/* Basic function declarations */
lxb_html_document_t* lxb_html_document_create(void);
void lxb_html_document_destroy(lxb_html_document_t *document);
lxb_status_t lxb_html_document_parse(lxb_html_document_t *document, 
                                    const lxb_char_t *html, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_HTML_H */
EOF

    # Create lexbor/url/url.h stub  
    mkdir -p "$DEPS_DIR/include/lexbor/url"
    cat > "$DEPS_DIR/include/lexbor/url/url.h" << 'EOF'
/* Minimal lexbor URL stub header for cross-compilation testing */
#ifndef LEXBOR_URL_H
#define LEXBOR_URL_H

#include "../core/core.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* URL scheme types */
typedef enum {
    LXB_URL_SCHEMEL_TYPE_FILE = 1,
    LXB_URL_SCHEMEL_TYPE_HTTP = 2,
    LXB_URL_SCHEMEL_TYPE_HTTPS = 3
} lxb_url_scheme_type_t;

/* URL structures */
typedef struct {
    lxb_url_scheme_type_t type;
} lxb_url_scheme_t;

typedef struct {
    size_t length;
    struct {
        const lxb_char_t *data;
    } str;
} lxb_url_path_t;

typedef struct lxb_url {
    lxb_url_scheme_t scheme;
    lxb_url_path_t path;
} lxb_url_t;

typedef struct lxb_url_parser {
    int dummy; /* stub field */
} lxb_url_parser_t;

/* Basic function declarations */
lxb_url_t* lxb_url_create(void);
void lxb_url_destroy(lxb_url_t *url);

/* URL parser functions */
lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *memory);
void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool self_destroy);
lxb_url_t* lxb_url_parse_with_parser(lxb_url_parser_t *parser, lxb_url_t *base, const lxb_char_t *input, size_t length);

/* URL utility functions */
lxb_url_path_t* lxb_url_path(lxb_url_t *url);
void lxb_url_serialize_path(lxb_url_path_t *path, lxb_status_t (*callback)(const lxb_char_t *, size_t, void *), void *ctx);

/* Compatibility macro for parser version */
#define lxb_url_parse(parser, base, input, length) lxb_url_parse_with_parser(parser, base, input, length)

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_URL_H */
EOF

    # Create stub implementation
    cat > "$DEPS_DIR/src/lexbor_stub.c" << 'EOF'
/* Minimal lexbor stub implementation */
#include "../include/lexbor/core/core.h"
#include "../include/lexbor/html/html.h"
#include "../include/lexbor/url/url.h"
#include <stdlib.h>

/* Core functions */
lxb_status_t lexbor_init(void) {
    return LXB_STATUS_OK;
}

void lexbor_terminate(void) {
    /* Stub implementation */
}

/* HTML functions */
lxb_html_document_t* lxb_html_document_create(void) {
    return (lxb_html_document_t*)malloc(sizeof(void*));
}

void lxb_html_document_destroy(lxb_html_document_t *document) {
    if (document) free(document);
}

lxb_status_t lxb_html_document_parse(lxb_html_document_t *document, 
                                    const lxb_char_t *html, size_t size) {
    (void)document; (void)html; (void)size;
    return LXB_STATUS_OK;
}

/* URL functions */
lxb_url_t* lxb_url_create(void) {
    lxb_url_t* url = (lxb_url_t*)malloc(sizeof(lxb_url_t));
    if (url) {
        url->scheme.type = LXB_URL_SCHEMEL_TYPE_FILE;
        url->path.length = 0;
        url->path.str.data = NULL;
    }
    return url;
}

void lxb_url_destroy(lxb_url_t *url) {
    if (url) free(url);
}

/* URL parser functions */
lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *memory) {
    (void)parser; (void)memory;
    return LXB_STATUS_OK;
}

void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool self_destroy) {
    (void)parser; (void)self_destroy;
}

lxb_url_t* lxb_url_parse_with_parser(lxb_url_parser_t *parser, lxb_url_t *base, const lxb_char_t *input, size_t length) {
    (void)parser; (void)base; (void)input; (void)length;
    return lxb_url_create();
}

/* URL utility functions */
lxb_url_path_t* lxb_url_path(lxb_url_t *url) {
    return url ? &url->path : NULL;
}

void lxb_url_serialize_path(lxb_url_path_t *path, lxb_status_t (*callback)(const lxb_char_t *, size_t, void *), void *ctx) {
    (void)path; (void)callback; (void)ctx;
}
EOF

    # Compile stub library
    cd "$DEPS_DIR/src"
    $TARGET_TRIPLET-gcc -I../include -c lexbor_stub.c -o lexbor_stub.o
    $TARGET_TRIPLET-ar rcs ../lib/liblexbor_static.a lexbor_stub.o
    cd - > /dev/null
    
    echo "lexbor stub library created (for compilation testing only)"
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
    # Also create the expected Windows-specific name for cross-compilation script
    cp libtree-sitter.a "libtree-sitter-windows.a"
    mkdir -p "../../$DEPS_DIR/include"
    cp -r lib/include/* "../../$DEPS_DIR/include/"
    
    cd - > /dev/null
    echo "Tree-sitter built successfully"
else
    echo "Tree-sitter already built for Windows"
fi

# Build lexbor for Windows
if [ ! -f "$DEPS_DIR/lib/liblexbor_static.a" ]; then
    echo "Building lexbor for Windows..."
    
    if ! build_lexbor_for_windows; then
        echo "Warning: lexbor build failed, falling back to stub implementation"
        cd "$SCRIPT_DIR"  # Return to script directory
        build_lexbor_stub
    else
        echo "lexbor built successfully"
    fi
else
    echo "lexbor already built for Windows"
fi

# Download and build GMP if not available
if [ ! -f "$DEPS_DIR/lib/libgmp.a" ]; then
    echo "Building full GMP for Windows..."
    
    # Try building full GMP first
    if build_gmp_full; then
        echo "✅ Full GMP built successfully"
    else
        echo "❌ Full GMP build failed, falling back to stub implementation"
        build_gmp_stub
    fi
else
    echo "GMP already built for Windows"
    
    # Check if it's a stub by size
    LIB_SIZE=$(stat -f%z "$DEPS_DIR/lib/libgmp.a" 2>/dev/null || stat -c%s "$DEPS_DIR/lib/libgmp.a" 2>/dev/null)
    if [ "$LIB_SIZE" -lt 100000 ]; then
        echo "Detected stub GMP library (${LIB_SIZE} bytes), attempting full build..."
        if build_gmp_full; then
            echo "✅ Upgraded to full GMP successfully"
        else
            echo "❌ Full GMP build failed, keeping stub implementation"
        fi
    else
        echo "Full GMP library detected (${LIB_SIZE} bytes)"
        # Check if gmp_sprintf is available
        if nm "$DEPS_DIR/lib/libgmp.a" 2>/dev/null | grep -q gmp_sprintf; then
            echo "✅ gmp_sprintf function is available in GMP library"
        else
            echo "⚠️  gmp_sprintf not found in GMP library"
        fi
    fi
fi

echo "Windows dependencies build completed!"

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
echo "- lexbor: $([ -f "$DEPS_DIR/lib/liblexbor_static.a" ] && echo "✓ Built ($([ -f "$DEPS_DIR/src/lexbor_stub.c" ] && echo "stub" || echo "real"))" || echo "✗ Missing")"
echo ""
echo "Next steps:"
echo "1. Run: ./compile-lambda-cross.sh build_lambda_windows_config.json"
echo ""
echo "To clean up intermediate files later, run: ./setup-windows-deps.sh clean"
