#!/usr/bin/env bash
# Report tree-sitter parser size metrics for the grammar-reduction campaign.
# Usage: utils/parser_stats.sh [label] [package_dir]
# Prints one markdown table row: label | parser.c | parser.o | STATE_COUNT | LARGE_STATE_COUNT | SYMBOL_COUNT
set -euo pipefail

LABEL="${1:-current}"
PKG="${2:-lambda/tree-sitter-lambda}"
PARSER_C="$PKG/src/parser.c"
PARSER_O="$PKG/src/parser.o"

[ -f "$PARSER_C" ] || { echo "parser.c not found: $PARSER_C" >&2; exit 1; }

define_val() { sed -n "s/^#define $1 \([0-9]*\).*/\1/p" "$PARSER_C" | head -1; }
fsize() { [ -f "$1" ] && stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null || echo 0; }

c_size=$(fsize "$PARSER_C")
# compile with the same flags the package sub-make uses, into a scratch object
o_size=0
if command -v cc >/dev/null; then
    tmp_o=$(mktemp -t parser_stats).o
    cc -I"$PKG/src" -std=c11 -fPIC -c "$PARSER_C" -o "$tmp_o" 2>/dev/null && o_size=$(fsize "$tmp_o")
    rm -f "$tmp_o"
fi
[ "$o_size" = 0 ] && [ -f "$PARSER_O" ] && o_size=$(fsize "$PARSER_O")

printf '| %s | %s | %s | %s | %s | %s |\n' \
    "$LABEL" \
    "$(printf "%'d" "$c_size")" \
    "$(printf "%'d" "$o_size")" \
    "$(printf "%'d" "$(define_val STATE_COUNT)")" \
    "$(printf "%'d" "$(define_val LARGE_STATE_COUNT)")" \
    "$(define_val SYMBOL_COUNT)"
