#!/usr/bin/env bash

# Freeze the Phase 1 Lambda-parser corpus from tracked source files.  The
# generated file is deliberately under ./temp/ so a parser POC never mutates
# source-controlled test inputs or writes outside the repository.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

output_path=${1:-temp/lambda-parser-poc/manifest.tsv}
mkdir -p "$(dirname "$output_path")"

scanner_hash=$(shasum -a 256 lambda/tree-sitter-lambda/src/scanner.c | awk '{print $1}')
grammar_shipped_hash=$(shasum -a 256 lambda/tree-sitter-lambda/grammar.js | awk '{print $1}')

{
    printf '# lambda-parser-poc-manifest-v1\n'
    printf '# git_commit\t%s\n' "$(git rev-parse HEAD)"
    printf '# scanner.c.sha256\t%s\n' "$scanner_hash"
    printf '# grammar.js.sha256\t%s\n' "$grammar_shipped_hash"
    printf 'path\tbytes\tsha256\n'
    while IFS= read -r source_path; do
        source_bytes=$(wc -c < "$source_path" | tr -d '[:space:]')
        source_hash=$(shasum -a 256 "$source_path" | awk '{print $1}')
        printf '%s\t%s\t%s\n' "$source_path" "$source_bytes" "$source_hash"
    done < <(git ls-files -- '*.ls')
} > "$output_path"

printf 'Lambda parser POC manifest: %s\n' "$output_path"
