#!/usr/bin/env bash

# Run the Phase 1 parser speed comparison.  Source files are preloaded by the
# harness; manifest generation and compilation are intentionally out of band.
# The Tree-sitter Lambda archive is supplied by the isolated lambda-cst profile.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

manifest_path=${1:-temp/lambda-parser-poc/manifest.tsv}
bash utils/lambda_parser_manifest.sh "$manifest_path"

mkdir -p temp/lambda-parser-poc
make --no-print-directory lambda-cst
benchmark_path=temp/lambda-parser-poc/lambda_parser_poc_perf
cc -std=c17 -O3 -DNDEBUG -march=native -I. -Ilambda/runtime/parser \
    -Ilambda/tree-sitter/lib/include \
    test/lambda_parser_poc_perf.c \
    lambda/runtime/parser/lambda_lexer.c lambda/runtime/parser/lambda_parser.c \
    lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a \
    -o "$benchmark_path"
"$benchmark_path" "$manifest_path"
