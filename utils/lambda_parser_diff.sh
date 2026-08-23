#!/usr/bin/env bash

# Run the reproducible valid-source side of the Phase 1 parser differential.
# The Tree-sitter Lambda archive is intentionally isolated in the lambda-cst
# build profile; normal Lambda profiles do not carry the reference grammar.
# Artifacts stay under ./temp/ because this POC must not modify grammar inputs.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

manifest_path=${1:-temp/lambda-parser-poc/manifest.tsv}
bash utils/lambda_parser_manifest.sh "$manifest_path"

mkdir -p temp/lambda-parser-poc
make --no-print-directory lambda-cst
checker_path=./lambda-cst.exe
"$checker_path" "$manifest_path"
