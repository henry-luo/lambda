#!/usr/bin/env bash

# Run the reproducible valid-source side of the Phase 1 parser differential.
# Artifacts stay under ./temp/ because this POC must not modify grammar inputs.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

manifest_path=${1:-temp/lambda-parser-poc/manifest.tsv}
bash utils/lambda_parser_manifest.sh "$manifest_path"

mkdir -p temp/lambda-parser-poc
checker_path=temp/lambda-parser-poc/lambda_parser_poc_diff
cc -std=c17 -O2 -I. -Ilambda/runtime/parser -Ilambda/tree-sitter/lib/include \
    test/lambda_parser_poc_diff.c \
    lambda/runtime/parser/lambda_lexer.c lambda/runtime/parser/lambda_parser.c \
    lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a \
    -o "$checker_path"
"$checker_path" "$manifest_path"
