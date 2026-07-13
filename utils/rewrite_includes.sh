#!/usr/bin/env bash
# Rewrite Radiant header includes to a consolidated target header.
#
# usage:
#   utils/rewrite_includes.sh <target-header> <old-header>...
#
# Rewrites #include "<old>" to #include "<target>" across radiant/ lambda/ test/,
# fixing relative paths for non-radiant files, then removes duplicate include
# lines within each touched file.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$#" -lt 2 ]; then
    echo "usage: $0 <target-header> <old-header>..." >&2
    exit 2
fi

TARGET="$1"
shift
mkdir -p "$ROOT/temp"
SCRATCH="$ROOT/temp/rewrite_includes.$$"
mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT
case "$TARGET" in
    radiant/*) target_radiant="${TARGET#radiant/}" ;;
    *) target_radiant="$TARGET" ;;
esac

target_for_file() {
    local file="$1"
    local dir="${file%/*}"
    local prefix=""
    local depth=1
    local i=0
    if [ "$dir" != "$file" ]; then
        depth=$(awk -F/ '{print NF}' <<EOF
$dir
EOF
)
    fi
    case "$file" in
        radiant/*/*)
            depth=$(awk -F/ '{print NF - 1}' <<EOF
$dir
EOF
)
            while [ "$i" -lt "$depth" ]; do
                prefix="${prefix}../"
                i=$((i + 1))
            done
            printf '%s%s\n' "$prefix" "$target_radiant"
            ;;
        radiant/*) printf '%s\n' "$target_radiant" ;;
        *)
            while [ "$i" -lt "$depth" ]; do
                prefix="${prefix}../"
                i=$((i + 1))
            done
            printf '%sradiant/%s\n' "$prefix" "$target_radiant"
            ;;
    esac
}

old_variants() {
    local old="$1"
    case "$old" in
        radiant/*) old="${old#radiant/}" ;;
    esac
    printf '%s\n' "$old"
    printf 'radiant/%s\n' "$old"
    local prefix=""
    local depth=1
    while [ "$depth" -le 5 ]; do
        prefix="${prefix}../"
        printf '%sradiant/%s\n' "$prefix" "$old"
        printf '%s%s\n' "$prefix" "$old"
        depth=$((depth + 1))
    done
}

collect_touched_files() {
    local old="$1"
    while IFS= read -r variant; do
        rg -l --glob '*.{c,cpp,h,hpp,mm}' --fixed-strings "#include \"$variant\"" \
            radiant lambda test 2>/dev/null || true
    done < <(old_variants "$old")
}

dedupe_includes() {
    local file="$1"
    local tmp="$SCRATCH/$(printf '%s' "$file" | tr '/ ' '__').tmp"
    awk '
        /^#include[ \t]+"/ {
            if ($0 == previous) {
                previous = $0
                next
            }
        }
        { print; previous = $0 }
    ' "$file" > "$tmp"
    if ! cmp -s "$file" "$tmp"; then
        mv "$tmp" "$file"
    else
        rm -f "$tmp"
    fi
}

TOUCHED="$SCRATCH/touched.txt"
: > "$TOUCHED"
for old in "$@"; do
    while IFS= read -r file; do
        [ -n "$file" ] || continue
        replacement="#include \"$(target_for_file "$file")\""
        while IFS= read -r variant; do
            perl -0pi -e "s{#include\\s+\"\\Q$variant\\E\"}{$replacement}g" "$file"
        done < <(old_variants "$old")
        printf '%s\n' "$file" >> "$TOUCHED"
    done < <(collect_touched_files "$old" | sort -u)
done

if [ -s "$TOUCHED" ]; then
    sort -u "$TOUCHED" | while IFS= read -r file; do
        dedupe_includes "$file"
        printf '%s\n' "$file"
    done
fi
