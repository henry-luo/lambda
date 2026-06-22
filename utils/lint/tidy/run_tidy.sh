#!/usr/bin/env bash
# run_tidy.sh — Phase 3 backend (type-aware narrowing) for the Lambda lint stack.
#
# Two rules, two sub-tools:
#
#   int-cast-type-aware-decl   →  clang-tidy + bugprone-narrowing-conversions
#                                 catches  `int x = float_expr;`
#                                 (implicit narrowing on initialization)
#
#   int-cast-type-aware        →  utils/lint/tidy/explicit_cast_check.py (libclang)
#                                 catches  `(int)float_expr`  and  `static_cast<int>(...)`
#                                 (explicit casts that bugprone-narrowing-conversions
#                                 deliberately skips — programmer-intentional)
#
# Both emit findings in the unified record shape ast-grep + alint use, plus a
# `backend: clang-tidy` tag. run.sh splices the stream into the existing
# severity / suppression / report pipeline.
#
# Scope: the same file set as the `no-int-cast-radiant` ast-grep rule. Other
# .cpp files (lambda/, lambda/js/, …) are intentionally out of scope for now —
# narrowing-conversion warnings explode without per-directory triage.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CLANG_TIDY="${CLANG_TIDY:-/opt/homebrew/opt/llvm/bin/clang-tidy}"
VENV_PY="$ROOT/utils/lint/tidy/.venv/bin/python3"
EXPLICIT_PY="$ROOT/utils/lint/tidy/explicit_cast_check.py"

# Subset of layout files that no-int-cast-radiant covers.
FILES=(
  radiant/layout_block.cpp        radiant/layout_inline.cpp
  radiant/layout_text.cpp         radiant/layout_positioned.cpp
  radiant/layout_flex.cpp         radiant/layout_flex_multipass.cpp
  radiant/layout_flex_measurement.cpp
  radiant/layout_table.cpp        radiant/layout_multicol.cpp
  radiant/layout_grid.cpp         radiant/layout_grid_multipass.cpp
  radiant/layout_form.cpp         radiant/layout_list.cpp
  radiant/layout_counters.cpp
  radiant/grid_sizing.cpp         radiant/grid_positioning.cpp
  radiant/grid_utils.cpp
  radiant/intrinsic_sizing.cpp
)

# Inline clang-tidy config — overrides the project's .clang-tidy by passing
# --checks. We want ONLY float→int narrowing, not int/int or int→float.
TIDY_CHECKS='-*,bugprone-narrowing-conversions'
TIDY_OPTS='{Checks: "-*,bugprone-narrowing-conversions", CheckOptions: [
  {key: bugprone-narrowing-conversions.WarnOnFloatingPointNarrowingConversion,         value: "1"},
  {key: bugprone-narrowing-conversions.WarnOnIntegerNarrowingConversion,               value: "0"},
  {key: bugprone-narrowing-conversions.WarnOnIntegerToFloatingPointNarrowingConversion, value: "0"},
  {key: bugprone-narrowing-conversions.WarnWithinTemplateInstantiation,                value: "0"},
  {key: bugprone-narrowing-conversions.PedanticMode,                                   value: "0"}
]}'

cd "$ROOT"

# Bail if the toolchain is missing — the rule simply produces no findings.
if [[ ! -x "$CLANG_TIDY" ]] || [[ ! -x "$VENV_PY" ]] || [[ ! -f "$EXPLICIT_PY" ]]; then
  exit 0
fi

# ───────── int-cast-type-aware-decl (implicit narrowing via clang-tidy) ─────────

tidy_out=$(
  for f in "${FILES[@]}"; do
    "$CLANG_TIDY" "$f" --config="$TIDY_OPTS" --checks="$TIDY_CHECKS" --quiet \
      -- -std=c++17 -I. -Ilib -Ilib/mem-pool/include -Ilambda -Iradiant -Iinclude -w \
      2>&1 | grep -E ':[0-9]+:[0-9]+: warning:.*bugprone-narrowing-conversions'
  done \
  | grep -vE "to '(float|double|long double)'"   # only int-class destinations
)

# Normalise clang-tidy text output to unified NDJSON.
# Each input line looks like:
#   /Users/.../radiant/layout_block.cpp:3076:51: warning: narrowing conversion from 'float' to 'int' [bugprone-narrowing-conversions]
printf '%s\n' "$tidy_out" | awk -v root="$ROOT/" '
  /:[0-9]+:[0-9]+: warning:.*bugprone-narrowing-conversions/ {
    rel = $0
    sub(root, "", rel)
    if (match(rel, /:[0-9]+:[0-9]+: warning: /) == 0) next
    file_part = substr(rel, 1, RSTART - 1)
    head      = substr(rel, RSTART, RLENGTH)         # ":line:col: warning: "
    tail      = substr(rel, RSTART + RLENGTH)        # message + [check]
    n = split(substr(head, 2), pos, ":")
    line = pos[1] + 0
    col  = pos[2] + 0
    sub(/ +\[bugprone-narrowing-conversions\]$/, "", tail)
    msg = tail
    gsub(/\\/, "\\\\", msg)
    gsub(/"/, "\\\"", msg)
    printf "{\"ruleId\":\"int-cast-type-aware-decl\",\"severity\":\"warning\",\"message\":\"%s — drop the narrowing init or mark with // INT_CAST_OK: <reason>\",\"file\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"column\":%d},\"end\":{\"line\":%d,\"column\":%d},\"byteOffset\":{\"start\":0,\"end\":0}},\"text\":\"%s\",\"lines\":\"\",\"metadata\":{\"suppress\":\"INT_CAST_OK\"},\"backend\":\"clang-tidy\"}\n",
      msg, file_part, line - 1, col - 1, line - 1, col - 1, msg
  }
'

# ───────── int-cast-type-aware (explicit casts via libclang) ─────────

"$VENV_PY" "$EXPLICIT_PY" --repo-root "$ROOT" "${FILES[@]}" 2>/dev/null
