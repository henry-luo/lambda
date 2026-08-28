#!/bin/sh
set -eu
base=""; phase_base=""; cap=317606
while [ "$#" -gt 0 ]; do case "$1" in
    --base) base=$2; shift 2 ;;
    --phase-base) phase_base=$2; shift 2 ;;
    --cap) cap=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
esac; done
[ -n "$base" ] || { echo "--base is required" >&2; exit 2; }
for commit in "$base" "$phase_base"; do
    [ -z "$commit" ] || git cat-file -e "$commit^{commit}" 2>/dev/null || { echo "anchor commit unavailable: $commit" >&2; exit 1; }
done
mkdir -p ./temp/ast_tune
files="./temp/ast_tune/loc_files.$$"; trap 'rm -f "$files"' 0 1 2 3 15
{ git ls-tree -r --name-only "$base" -- lambda/runtime lambda/js; git ls-files -- lambda/runtime lambda/js; } | sort -u | awk '/\.(c|cc|cpp|h|hpp)$/ { print }' > "$files"
count_tree() {
    total=0; while IFS= read -r path; do
        [ -n "$path" ] || continue
        if [ "$1" = base ]; then lines=$(git show "$base:$path" 2>/dev/null | wc -l | tr -d ' '); else [ -f "$path" ] || continue; lines=$(wc -l < "$path" | tr -d ' '); fi
        total=$((total + lines))
    done < "$files"; echo "$total"
}
baseline=$(count_tree base); candidate=$(count_tree candidate); delta=$((candidate - baseline))
echo "AST_TUNE_LOC baseline=$baseline candidate=$candidate delta=$delta threshold=$cap"
[ "$base" != "e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3" ] || [ "$baseline" -eq 319606 ] || { echo "unexpected formal anchor LOC: $baseline" >&2; exit 1; }
[ "$candidate" -le "$cap" ] || { echo "runtime LOC gate failed" >&2; exit 1; }
[ -z "$phase_base" ] && exit 0
untracked=$(git ls-files --others --exclude-standard | awk '/\.(c|cc|cpp|h|hpp|inc|inl|sh|py|js|mjs|ts|tsx|ls|json)$/ || /(^|\/)(Makefile|GNUmakefile)$/ { print; exit }')
[ -z "$untracked" ] || { echo "untracked source prevents a complete phase report: $untracked" >&2; exit 1; }
git diff --numstat "$phase_base" -- | awk '
function source(p) { return p ~ /\.(c|cc|cpp|h|hpp|inc|inl|sh|py|js|mjs|ts|tsx|ls|json)$/ || p ~ /(^|\/)(Makefile|GNUmakefile)$/ }
function cpp(p) { return p ~ /\.(c|cc|cpp|h|hpp|inc|inl)$/ }
source($3) { source_add += $1; source_del += $2 } cpp($3) { cpp_add += $1; cpp_del += $2 }
END { source_delta = source_add - source_del; cpp_delta = cpp_add - cpp_del
    printf "AST_TUNE_PHASE_CPP base=%s added=%d removed=%d delta=%d\n", base, cpp_add, cpp_del, cpp_delta; printf "AST_TUNE_PHASE_SOURCE base=%s added=%d removed=%d delta=%d\n", base, source_add, source_del, source_delta
    exit cpp_delta > 0 || source_delta > 0
}' base="$phase_base"
