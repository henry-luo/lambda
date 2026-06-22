#!/usr/bin/env bash
# run_tidy.sh — clang-tidy / libclang backend for the Lambda lint stack.
#
# Two passes, both producing unified NDJSON with `backend: clang-tidy`:
#
#  1. Project-wide bug-finding pass:
#       Runs `bugprone-*,clang-analyzer-*,cert-*` (minus the families the
#       project's .clang-tidy explicitly disables) across every .cpp under
#       lambda/ and radiant/. Honours both clang-tidy's native `// NOLINT(...)`
#       markers and our project markers (`TIDY_OK`).
#       Rule id per finding: `tidy-<check-name>` (e.g. tidy-bugprone-macro-parentheses).
#
#  2. Explicit float→int cast pass (libclang AST):
#       utils/lint/tidy/explicit_cast_check.py — catches `(int)float_expr` /
#       `static_cast<int>(float_expr)` that pass (1) ignores by design.
#       Rule id: `int-cast-type-aware`. Suppressed by `// INT_CAST_OK`.
#
# Both passes emit records the run.sh pipeline consumes — the severity gate,
# suppression filter (per-rule `metadata.suppress`), and Report_NNN.* writer
# all apply with no further plumbing.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CLANG_TIDY="${CLANG_TIDY:-/opt/homebrew/opt/llvm/bin/clang-tidy}"
VENV_PY="$ROOT/utils/lint/tidy/.venv/bin/python3"
EXPLICIT_PY="$ROOT/utils/lint/tidy/explicit_cast_check.py"
JOBS="${LINT_TIDY_JOBS:-8}"   # parallel clang-tidy invocations

cd "$ROOT"

if [[ ! -x "$CLANG_TIDY" ]]; then
  exit 0
fi

# ───────── pass 1: project-wide bug-finding (parallel) ─────────
#
# Checks: every bugprone-* / clang-analyzer-* / cert-* check, minus four the
# project's .clang-tidy already disables (kept disabled here for parity with
# `make tidy` of the past) and minus bugprone-narrowing-conversions for non-
# float narrowing (we keep float→int only — the truncation case).
TIDY_CHECKS='-*,bugprone-*,clang-analyzer-*,cert-*'
# Matches the project's .clang-tidy disables.
TIDY_CHECKS+=',-bugprone-easily-swappable-parameters'
TIDY_CHECKS+=',-cert-err58-cpp'
# Empirically too noisy on this codebase:
#   cert-err33-c             fires on every fopen/fclose/fseek return that's
#                            not stored — ~1100 findings, mostly false signal.
#   bugprone-reserved-identifier
#                            fires on the project's `_foo` prefix convention.
TIDY_CHECKS+=',-cert-err33-c'
TIDY_CHECKS+=',-bugprone-reserved-identifier'
# cert-dcl37-c / cert-dcl51-cpp are aliases of bugprone-reserved-identifier;
# disable for the same reason.
TIDY_CHECKS+=',-cert-dcl37-c'
TIDY_CHECKS+=',-cert-dcl51-cpp'
TIDY_OPTS='{Checks: "'"$TIDY_CHECKS"'", CheckOptions: [
  {key: bugprone-narrowing-conversions.WarnOnFloatingPointNarrowingConversion,         value: "1"},
  {key: bugprone-narrowing-conversions.WarnOnIntegerNarrowingConversion,               value: "0"},
  {key: bugprone-narrowing-conversions.WarnOnIntegerToFloatingPointNarrowingConversion, value: "0"},
  {key: bugprone-narrowing-conversions.WarnWithinTemplateInstantiation,                value: "0"},
  {key: bugprone-narrowing-conversions.PedanticMode,                                   value: "0"}
]}'

# Parallel run. xargs handles file enumeration and the parallelism — no need
# to materialise the file list in a bash array (macOS bash 3.2 lacks mapfile).
# Output goes to a temp file because xargs interleaves stdouts otherwise.
TIDY_OUT=$(mktemp)
trap 'rm -f "$TIDY_OUT"' EXIT

find lambda radiant -type f -name '*.cpp' -not -path '*/tree-sitter*/*' 2>/dev/null \
  | xargs -n 1 -P "$JOBS" -I {} "$CLANG_TIDY" {} \
        --config="$TIDY_OPTS" \
        --quiet \
        -- -std=c++17 -I. -Ilib -Ilib/mem-pool/include -Ilambda -Iradiant -Iinclude -w \
      2>&1 \
  | grep -E ':[0-9]+:[0-9]+: warning:.*\[(bugprone|clang-analyzer|cert)-' \
  > "$TIDY_OUT"

# Normalise output → unified NDJSON.
# Honour BOTH our project marker (`// TIDY_OK`) and clang-tidy's native
# `// NOLINT`/`// NOLINT(check-name)`. Suppression is per-line: read the
# source line and skip if any marker is on it.
awk -v root="$ROOT/" '
  function read_line(file, line,    cmd, l) {
    cmd = "sed -n " line "p " file
    cmd | getline l
    close(cmd)
    return l
  }
  /:[0-9]+:[0-9]+: warning:.*\[(bugprone|clang-analyzer|cert)-/ {
    rel = $0
    sub(root, "", rel)
    if (match(rel, /:[0-9]+:[0-9]+: warning: /) == 0) next
    file_part = substr(rel, 1, RSTART - 1)
    head      = substr(rel, RSTART, RLENGTH)
    tail      = substr(rel, RSTART + RLENGTH)
    n = split(substr(head, 2), pos, ":")
    line = pos[1] + 0
    col  = pos[2] + 0
    # Strip the trailing [check-name] (single-check form: `[bugprone-foo]`)
    # OR [check-a,check-b,check-c] (multi-check / alias form: pick first as id,
    # since clang-tidy listed it first as the primary attribution).
    if (match(tail, / \[[a-zA-Z][a-zA-Z0-9.,-]+\]$/) > 0) {
      tag = substr(tail, RSTART + 2, RLENGTH - 3)
      msg = substr(tail, 1, RSTART - 1)
      # First check name in a comma-separated list.
      i = index(tag, ",")
      check = (i > 0 ? substr(tag, 1, i - 1) : tag)
    } else {
      check = "unknown"
      msg   = tail
    }
    # Skip if NOLINT or our project marker is on the offending line.
    src = read_line(root file_part, line)
    if (src ~ /\/\/[[:space:]]*NOLINT/) next
    if (src ~ /\/\/[[:space:]]*TIDY_OK/) next
    # Skip narrowing-conversions findings to NON-int (already filtered to
    # int by default since we set FP→int=1, but partial-parse can leak).
    if (check == "bugprone-narrowing-conversions" && msg !~ /to .int.|to .unsigned|to .signed|to .short|to .long|to .char/) next

    gsub(/\\/, "\\\\", msg);  gsub(/"/, "\\\"", msg)
    gsub(/\\/, "\\\\", src);  gsub(/"/, "\\\"", src)
    printf "{\"ruleId\":\"tidy-%s\",\"severity\":\"warning\",\"message\":\"%s — see clang-tidy `%s` (suppress with `// NOLINT(%s)` or `// TIDY_OK: <reason>`).\",\"file\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"column\":%d},\"end\":{\"line\":%d,\"column\":%d},\"byteOffset\":{\"start\":0,\"end\":0}},\"text\":\"%s\",\"lines\":\"%s\",\"metadata\":{\"suppress\":\"TIDY_OK\"},\"backend\":\"clang-tidy\"}\n",
      check, msg, check, check, file_part, line - 1, col - 1, line - 1, col - 1, msg, src
  }
' "$TIDY_OUT"

# ───────── pass 2: explicit float→int cast (libclang AST) ─────────
# Limited to the same 19 layout files where this rule's payoff was empirically
# validated (60-78% INT_CAST_OK marker reclaim). Broader scope = future work.

if [[ -x "$VENV_PY" && -f "$EXPLICIT_PY" ]]; then
  EXPLICIT_FILES=(
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
  "$VENV_PY" "$EXPLICIT_PY" --repo-root "$ROOT" "${EXPLICIT_FILES[@]}" 2>/dev/null
fi
