#!/bin/sh
set -eu

base=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --base) base=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$base" ] || { echo "--base is required" >&2; exit 2; }
git cat-file -e "$base^{commit}" 2>/dev/null || { echo "anchor commit unavailable: $base" >&2; exit 1; }

files="./temp/ast_tune/loc_files.$$"
trap 'rm -f "$files"' 0 1 2 3 15
{
    git ls-tree -r --name-only "$base" -- lambda/runtime lambda/js
    git ls-files -- lambda/runtime lambda/js
} | sort -u | awk '/\.(c|cc|cpp|h|hpp)$/ { print }' > "$files"

count_tree() {
    total=0
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if [ "$1" = base ]; then
            lines=$(git show "$base:$path" 2>/dev/null | wc -l | tr -d ' ')
        else
            [ -f "$path" ] || continue
            lines=$(wc -l < "$path" | tr -d ' ')
        fi
        total=$((total + lines))
    done < "$files"
    echo "$total"
}

baseline=$(count_tree base)
candidate=$(count_tree candidate)
delta=$((candidate - baseline))
echo "AST_TUNE_LOC baseline=$baseline candidate=$candidate delta=$delta threshold=317606"
[ "$baseline" -eq 319606 ] || { echo "unexpected anchor LOC: $baseline" >&2; exit 1; }
[ "$candidate" -le 317606 ] || { echo "runtime LOC gate failed" >&2; exit 1; }
