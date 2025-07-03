#!/bin/bash

# Test script for lambda functionality
# This script compiles and runs the lambda tests using Criterion

echo "Building and running lambda tests..."

# Get all the object files from the existing build
OBJECT_FILES=(
    "../build/parser.o"
    "../build/parse.o"
    "../build/strbuf.o"
    "../build/strview.o"
    "../build/arraylist.o"
    "../build/file.o"
    "../build/hashmap.o"
    "../build/variable.o"
    "../build/buffer.o"
    "../build/utils.o"
    "../build/url.o"
    "../build/runner.o"
    "../build/transpile.o"
    "../build/build_ast.o"
    "../build/mir.o"
    "../build/pack.o"
    "../build/print.o"
    "../build/lambda-eval.o"
    "../build/lambda-mem.o"
    "../build/input.o"
    "../build/input-json.o"
    "../build/input-csv.o"
    "../build/input-ini.o"
    "../build/input-xml.o"
    "../build/input-yaml.o"
    "../build/input-md.o"
    "../build/input-toml.o"
    "../build/input-html.o"
    "../build/input-latex.o"
    "../build/format.o"
    "../build/format-json.o"
    "../build/format-md.o"
)

# Change to test directory
cd "$(dirname "$0")"

# Compile the test
echo "Compiling test_lambda..."
clang -o test_lambda.exe test_lambda.c "${OBJECT_FILES[@]}" \
    -I../lib/mem-pool/include \
    -I../lambda \
    -I../lib \
    -I../lambda/tree-sitter/lib/include \
    -I/usr/local/include \
    -I/opt/homebrew/include \
    -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
    -L/opt/homebrew/lib \
    -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib \
    ../lambda/tree-sitter/libtree-sitter.a \
    /usr/local/lib/liblexbor_static.a \
    /usr/local/lib/libmir.a \
    /usr/local/lib/libzlog.a \
    -lgmp \
    -lcriterion \
    -fms-extensions

if [ $? -eq 0 ]; then
    echo "Build successful! Running tests..."
    echo "=================================="
    ./test_lambda.exe --verbose
else
    echo "Build failed!"
    exit 1
fi
