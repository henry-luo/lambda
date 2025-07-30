#!/bin/bash

# Simple test script to debug WASM compilation issues
set -e

WASI_SDK_PATH="/opt/wasi-sdk"
CC="$WASI_SDK_PATH/bin/clang"
BUILD_DIR="build_wasm_test"

echo "Creating build directory..."
mkdir -p "$BUILD_DIR"

echo "Testing single file compilation..."
$CC \
  -Ilambda/tree-sitter/lib/include \
  -Ilambda/tree-sitter-lambda/bindings/c \
  --sysroot=$WASI_SDK_PATH/share/wasi-sysroot \
  -fms-extensions \
  -O2 \
  -DWASM_BUILD \
  -D_POSIX_C_SOURCE=200809L \
  -MMD -MP \
  -c lib/strbuf.c \
  -o "$BUILD_DIR/strbuf.o" \
  2>&1

echo "Single file compilation successful!"

echo "Testing input file compilation..."
$CC \
  -Iwasm-deps/include \
  -Ilambda/tree-sitter/lib/include \
  -Ilambda/tree-sitter-lambda/bindings/c \
  --sysroot=$WASI_SDK_PATH/share/wasi-sysroot \
  -fms-extensions \
  -O2 \
  -DWASM_BUILD=1 \
  -DCROSS_COMPILE=1 \
  -D_POSIX_C_SOURCE=200809L \
  -MMD -MP \
  -c lambda/input/input-mark.c \
  -o "$BUILD_DIR/input-mark.o" \
  2>&1

echo "Input file compilation successful!"

echo "All tests passed!"
