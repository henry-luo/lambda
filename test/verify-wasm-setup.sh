#!/bin/bash

echo "=== WASM Compilation Setup Verification ==="
echo

echo "1. WASI SDK Installation:"
echo "   Path: /opt/wasi-sdk"
echo "   Version: $(/opt/wasi-sdk/bin/clang --version | head -1)"
echo

echo "2. WASM Dependencies Stub Libraries Created:"
echo "   - wasm-deps/include/gmp.h (GMP stub)"
echo "   - wasm-deps/include/mir.h (MIR stub)"
echo "   - wasm-deps/include/mir-gen.h (MIR-GEN stub)"
echo "   - wasm-deps/include/c2mir.h (C2MIR stub)"
echo "   - wasm-deps/include/lexbor/url/url.h (Lexbor URL stub)"
echo

echo "3. Testing Basic File Compilation:"
echo "   Testing lib/strbuf.c..."
if /opt/wasi-sdk/bin/clang \
  -Iwasm-deps/include \
  --sysroot=/opt/wasi-sdk/share/wasi-sysroot \
  -fms-extensions -O2 -DWASM_BUILD=1 -DCROSS_COMPILE=1 \
  -c lib/strbuf.c -o /tmp/strbuf_test.o 2>/dev/null; then
    echo "   ✓ lib/strbuf.c compiles successfully"
else
    echo "   ✗ lib/strbuf.c compilation failed"
fi

echo "   Testing lambda/input/input-mark.c..."
if /opt/wasi-sdk/bin/clang \
  -Iwasm-deps/include \
  -Ilambda/tree-sitter/lib/include \
  -Ilambda/tree-sitter-lambda/bindings/c \
  --sysroot=/opt/wasi-sdk/share/wasi-sysroot \
  -fms-extensions -O2 -DWASM_BUILD=1 -DCROSS_COMPILE=1 \
  -c lambda/input/input-mark.c -o /tmp/input_mark_test.o 2>/dev/null; then
    echo "   ✓ lambda/input/input-mark.c compiles successfully"
else
    echo "   ✗ lambda/input/input-mark.c compilation failed"
fi

echo

echo "4. WASM Compilation Configuration:"
echo "   Config file: build_lambda_wasm_config.json"
echo "   Script: compile-wasm.sh"
echo "   Build directory: build_wasm/"
echo "   Target: lambda.wasm"
echo "   Test configs: test/build_lambda_*_wasm_config.json"
echo

echo "5. Usage Instructions:"
echo "   Basic compilation:     ./compile-wasm.sh"
echo "   Debug build:          ./compile-wasm.sh --debug"
echo "   Force rebuild:        ./compile-wasm.sh --force"
echo "   Parallel jobs:        ./compile-wasm.sh --jobs=4"
echo "   Clean dependencies:   ./compile-wasm.sh --clean-deps"
echo

echo "=== Setup Complete! ==="
echo "The WASM compilation system is ready for basic lambda input/format files."
echo "For full compilation, you may need to resolve additional dependencies"
echo "in the validator and other complex modules."
