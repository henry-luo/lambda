#!/usr/bin/env bash

# Run the JS/TS valid-source differential through the isolated lambda-cst
# profile, keeping Tree-sitter grammar archives out of normal Lambda builds.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

manifest_path=${1:-temp/js-parser-poc/manifest.tsv}
bash utils/js_parser_manifest.sh "$manifest_path"

make --no-print-directory lambda-cst
./lambda-cst.exe --js "$manifest_path"
