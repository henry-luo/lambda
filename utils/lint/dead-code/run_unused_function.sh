#!/usr/bin/env bash
# run_unused_function.sh — Phase 4 backend (hybrid unused-function check).
#
# Implements Stages 1 + 2 of the proposal §8.3 design. Stage 3 (libclang USR
# verification) is **deferred**: the lexical Stage 1+2 signal on this codebase
# is actionable as-is, and the ~60 s libclang cost isn't justified by the
# measured false-positive rate. If FP rate proves higher in practice, the
# Stage 3 hook lands as a follow-up using the libclang infrastructure pattern
# from utils/lint/tidy/explicit_cast_check.py.
#
# Performance strategy: every ast-grep query (definition gather + 4 exclusion
# passes) runs in a SINGLE invocation via the `---` rule separator, so the
# codebase is parsed once instead of five times.  The grep word-frequency
# count runs in parallel with the ast-grep pass.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
mkdir -p "$ROOT/temp"
# keep lint scratch data inside the repo temp dir so policy checks never touch /tmp.
TMP=$(mktemp -d "$ROOT/temp/unused-function.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT"

need() { command -v "$1" >/dev/null 2>&1 || { echo "unused-fn: $1 not found" >&2; exit 127; }; }
need ast-grep
need jq
need grep

SGCONFIG="$ROOT/sgconfig.yml"

# ───────── Stage 1+2 (combined) — single ast-grep pass over the tree ─────────

# Inline-rules document with 5 rules, separated by `---`:
#   _ufn_defs        every function definition's NAME + location
#   _ufn_virt        names of virtual/override/final methods
#   _ufn_extc        names inside extern "C" blocks
#   _ufn_used        names with __attribute__((used)) / [[gnu::used]] / [[maybe_unused]]
#   _ufn_op          operator overloads
ASTGREP_RULES=$(cat <<'RULES'
id: _ufn_defs
language: cpp
severity: info
message: def
rule:
  kind: function_definition
  has:
    kind: function_declarator
    has:
      any:
        - { kind: identifier, pattern: $NAME }
        - { kind: field_identifier, pattern: $NAME }
files:
  - "lambda/**/*.{c,cpp,h,hpp}"
  - "radiant/**/*.{c,cpp,h,hpp}"
ignores:
  - "**/tree-sitter*/**"
---
id: _ufn_virt
language: cpp
severity: info
message: v
rule:
  kind: function_definition
  has:
    kind: function_declarator
    has:
      any:
        - { kind: identifier, pattern: $NAME }
        - { kind: field_identifier, pattern: $NAME }
  regex: "(\\bvirtual\\b|\\boverride\\b|\\bfinal\\b)"
files:
  - "lambda/**/*.{c,cpp,h,hpp}"
  - "radiant/**/*.{c,cpp,h,hpp}"
ignores:
  - "**/tree-sitter*/**"
---
id: _ufn_extc
language: cpp
severity: info
message: e
rule:
  kind: function_definition
  inside: { kind: linkage_specification }
  has:
    kind: function_declarator
    has: { any: [{ kind: identifier, pattern: $NAME }] }
files:
  - "lambda/**/*.{c,cpp,h,hpp}"
  - "radiant/**/*.{c,cpp,h,hpp}"
ignores:
  - "**/tree-sitter*/**"
---
id: _ufn_used
language: cpp
severity: info
message: u
rule:
  kind: function_definition
  has:
    kind: function_declarator
    has:
      any:
        - { kind: identifier, pattern: $NAME }
        - { kind: field_identifier, pattern: $NAME }
  regex: "(__attribute__\\(\\(used\\)\\)|gnu::used|maybe_unused)"
files:
  - "lambda/**/*.{c,cpp,h,hpp}"
  - "radiant/**/*.{c,cpp,h,hpp}"
ignores:
  - "**/tree-sitter*/**"
---
id: _ufn_op
language: cpp
severity: info
message: o
rule:
  kind: function_definition
  has:
    kind: function_declarator
    has: { kind: operator_name, pattern: $NAME }
files:
  - "lambda/**/*.{c,cpp,h,hpp}"
  - "radiant/**/*.{c,cpp,h,hpp}"
ignores:
  - "**/tree-sitter*/**"
RULES
)

# Kick off ast-grep and grep in parallel — they share no input/output.
(
  ast-grep scan -c "$SGCONFIG" --inline-rules "$ASTGREP_RULES" --json=stream 2>/dev/null \
    | jq -r '[.ruleId,
              .metaVariables.single.NAME.text // "",
              .file // "",
              ((.range.start.line // 0) + 1 | tostring)] | @tsv' \
    > "$TMP/ast.tsv"
) &
ASTGREP_PID=$!

(
  grep -rohE '\b[A-Za-z_][A-Za-z0-9_]*\b' lambda radiant \
    --include='*.cpp' --include='*.hpp' --include='*.c' --include='*.h' \
    --exclude-dir='tree-sitter' \
    --exclude-dir='tree-sitter-lambda' \
    --exclude-dir='tree-sitter-javascript' \
    2>/dev/null \
    | sort | uniq -c | awk '{print $2"\t"$1}' | sort -k1,1 \
    > "$TMP/wordcounts.txt"
) &
GREP_PID=$!

wait $ASTGREP_PID $GREP_PID

# Split the combined ast.tsv into per-rule files (parens around the redirect
# target — required by BSD awk, harmless on gawk).
awk -F'\t' -v out="$TMP" '
  $1 == "_ufn_defs"  { print $2 "\t" $3 "\t" $4 >> (out"/defs_full.tsv") }
  $1 == "_ufn_virt"  { print $2                  >> (out"/excl_virtual.txt") }
  $1 == "_ufn_extc"  { print $2                  >> (out"/excl_extern_c.txt") }
  $1 == "_ufn_used"  { print $2                  >> (out"/excl_used.txt") }
  $1 == "_ufn_op"    { print $2                  >> (out"/excl_operator.txt") }
' "$TMP/ast.tsv"

# ast-grep versions can legally return no rows for a rule; keep downstream
# join/sort stages deterministic by materializing every expected intermediate.
for f in defs_full.tsv excl_virtual.txt excl_extern_c.txt excl_used.txt excl_operator.txt; do
  [[ -e "$TMP/$f" ]] || : > "$TMP/$f"
done

# Dedup + sort each.
for f in defs_full.tsv excl_virtual.txt excl_extern_c.txt excl_used.txt excl_operator.txt; do
  sort -u "$TMP/$f" -o "$TMP/$f"
done

# Hard-coded allowlist.
cat > "$TMP/excl_allowlist.txt" <<'EOF'
main
WinMain
wmain
DllMain
_main
TEST
TEST_F
TEST_P
EOF

# Lines carrying UNUSED_FUNCTION_OK marker — extract identifier names from them.
grep -rhn 'UNUSED_FUNCTION_OK' lambda radiant \
  --include='*.cpp' --include='*.hpp' --include='*.c' --include='*.h' \
  --exclude-dir='tree-sitter*' 2>/dev/null \
  | grep -oE '\b[A-Za-z_][A-Za-z0-9_]*\b' \
  | sort -u > "$TMP/excl_suppressed.txt"

# Unique definition names.
[[ -s "$TMP/defs_full.tsv" ]] && cut -f1 "$TMP/defs_full.tsv" | sort -u > "$TMP/defs.txt" \
                              || : > "$TMP/defs.txt"

# Candidates: function names whose total codebase occurrence count is 1.
join "$TMP/defs.txt" "$TMP/wordcounts.txt" \
  | awk '$2 == 1 {print $1}' > "$TMP/candidates_raw.txt"

# Apply exclusions.
cat "$TMP"/excl_*.txt 2>/dev/null | sort -u > "$TMP/exclusions.txt"
comm -23 "$TMP/candidates_raw.txt" "$TMP/exclusions.txt" > "$TMP/candidates.txt"

# ───────── emit unified NDJSON ─────────
# Look up each candidate's definition site from defs_full.tsv.
sort "$TMP/candidates.txt" > "$TMP/candidates_sorted.txt"
sort -k1,1 -t$'\t' "$TMP/defs_full.tsv" > "$TMP/defs_full_sorted.tsv"

join -t$'\t' "$TMP/candidates_sorted.txt" "$TMP/defs_full_sorted.tsv" \
  | awk -F'\t' '{
      name=$1; file=$2; line=$3
      printf "{\"ruleId\":\"unused-function\",\"severity\":\"warning\",\"message\":\"Function `%s` is defined but never referenced — likely dead code. Remove, or mark with // UNUSED_FUNCTION_OK: <reason> if reachable via macro-synthesized dispatch, dlsym, or external linkage.\",\"file\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"column\":0},\"end\":{\"line\":%d,\"column\":0},\"byteOffset\":{\"start\":0,\"end\":0}},\"text\":\"%s\",\"lines\":\"\",\"metadata\":{\"suppress\":\"UNUSED_FUNCTION_OK\"},\"backend\":\"hybrid\"}\n",
        name, file, line - 1, line - 1, name
    }'
