#!/usr/bin/env bash

# Freeze the JS/TS reference-parser corpus for the isolated lambda-cst profile.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

output_path=${1:-temp/js-parser-poc/manifest.tsv}
mkdir -p "$(dirname "$output_path")"

{
    printf '# js-parser-poc-manifest-v1\n'
    printf '# git_commit\t%s\n' "$(git rev-parse HEAD)"
    printf 'language\tpath\tbytes\tsha256\n'
    git ls-files -- '*.js' '*.ts' | while IFS= read -r source_path; do
        case "$source_path" in
            test/js/*.js) language=javascript ;;
            test/ts/*.ts) language=typescript ;;
            *) continue ;;
        esac
        source_bytes=$(wc -c < "$source_path" | tr -d '[:space:]')
        source_hash=$(shasum -a 256 "$source_path" | awk '{print $1}')
        printf '%s\t%s\t%s\t%s\n' "$language" "$source_path" "$source_bytes" "$source_hash"
    done
} > "$output_path"

printf 'JS/TS parser POC manifest: %s\n' "$output_path"
