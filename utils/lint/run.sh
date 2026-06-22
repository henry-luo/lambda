#!/usr/bin/env bash
# Unified project-policy linter for the Lambda / Radiant codebase.
#
# Drives ast-grep with rule-scoped inline suppression and structured reporting.
# Pattern rules live in utils/lint/rules/**/*.yml; each rule may declare a
# `metadata.suppress: <MARKER>` and a finding is dropped when its source line
# carries that marker. Structural code checks (genuine coverage invariants that
# no pattern tool can express) are dispatched after the pattern pass.
#
# Usage:
#   utils/lint/run.sh                 # fast pass: ast-grep + alint + structural
#   utils/lint/run.sh --with-tidy     # add the clang-tidy backend (slow ~3-4 min)
#   utils/lint/run.sh --rule <regex>  # filter to one rule or family; auto-enables
#                                     # --with-tidy when the regex targets tidy-*
#   utils/lint/run.sh --report        # also write Report_NNN.{md,json,tsv}
#   utils/lint/run.sh --format github # GitHub Actions annotations
#   utils/lint/run.sh --list          # list rule + structural ids
#
# Make targets: `make lint` (fast) and `make lint-full` (adds clang-tidy).
#
# Exit: non-zero on any unsuppressed error-level finding or structural failure.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SGCONFIG="$ROOT/sgconfig.yml"
RULES_DIR="$ROOT/utils/lint/rules"
REPORT_DIR="$ROOT/test_output/lint"
SCAN_ROOTS=(lambda radiant lib test)

# Structural checks: "<id>:<command>". Each runs after the pattern pass and
# counts as failed iff the command exits non-zero.
STRUCTURAL_CHECKS=(
  "state-machine:python3 $ROOT/utils/check_state_machine.py"
  # ls-test-has-golden moved from Python to alint (see .alint.yml).
  # `check_state_machine.py` stays — it parses C++ enum tables and correlates,
  # which neither ast-grep nor alint can express.
)

# ---------- arg parsing ----------
RULE_FILTER=""; WRITE_REPORT=0; NO_STRUCTURAL=0; STRUCTURAL_ONLY=0
FORMAT="pretty"; LIST=0
WITH_TIDY=0   # clang-tidy backend OFF by default (slow, ~3-4 min) — opt in via --with-tidy

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rule)            RULE_FILTER="$2"; shift 2;;
    --report)          WRITE_REPORT=1; shift;;
    --no-structural)   NO_STRUCTURAL=1; shift;;
    --structural-only) STRUCTURAL_ONLY=1; shift;;
    --with-tidy)       WITH_TIDY=1; shift;;
    --format)          FORMAT="$2"; shift 2;;
    --list)            LIST=1; shift;;
    -h|--help)         usage; exit 0;;
    *)                 echo "lint: unknown arg '$1'" >&2; usage >&2; exit 2;;
  esac
done

# Auto-enable tidy when filtering to a tidy rule explicitly — otherwise
# `make lint ARGS='--rule ^tidy-bugprone'` would silently produce nothing.
if [[ -n "$RULE_FILTER" ]] && [[ "$RULE_FILTER" == *tidy* || "$RULE_FILTER" == *int-cast-type-aware* ]]; then
  WITH_TIDY=1
fi

# ---------- dependency checks ----------
need() { command -v "$1" >/dev/null 2>&1 || { echo "lint: $1 not found ($2)" >&2; exit 127; }; }
need ast-grep "brew install ast-grep"
need jq       "brew install jq"

# ---------- --list ----------
if (( LIST )); then
  echo "ast-grep rules (pattern):"
  for f in "$RULES_DIR"/c-cpp/*.yml; do
    rid=$(awk -F': *' '/^id:/{print $2; exit}' "$f")
    printf "  %-34s %s\n" "$rid" "${f#$ROOT/}"
  done
  if command -v alint >/dev/null 2>&1 && [[ -f "$ROOT/.alint.yml" ]]; then
    echo "alint rules (structural):"
    # `alint list -q` emits two whitespace-separated columns: level, id
    (cd "$ROOT" && alint list -q 2>/dev/null | awk '{printf "  %-34s .alint.yml\n", $2}')
  fi
  if [[ -x "$ROOT/utils/lint/tidy/run_tidy.sh" ]]; then
    echo "clang-tidy / libclang rules (type-aware):"
    echo "  int-cast-type-aware                utils/lint/tidy/explicit_cast_check.py"
    echo "  int-cast-type-aware-decl           utils/lint/tidy/run_tidy.sh + bugprone-narrowing-conversions"
  fi
  if [[ -x "$ROOT/utils/lint/dead-code/run_unused_function.sh" ]]; then
    echo "hybrid rules (Phase 4):"
    echo "  unused-function                    utils/lint/dead-code/run_unused_function.sh"
  fi
  echo "structural Python checks:"
  for entry in "${STRUCTURAL_CHECKS[@]}"; do echo "  ${entry%%:*}"; done
  if [[ -x "$ROOT/utils/lint/manifest_check.sh" ]]; then
    echo
    "$ROOT/utils/lint/manifest_check.sh"
  fi
  exit 0
fi

# ---------- ast-grep scan (pattern rules) ----------
RAW=""
if (( ! STRUCTURAL_ONLY )); then
  sg_args=(scan -c "$SGCONFIG" --include-metadata --json=stream)
  [[ -n "$RULE_FILTER" ]] && sg_args+=(--filter "$RULE_FILTER")
  sg_args+=("${SCAN_ROOTS[@]}")
  # ast-grep exits non-zero when it finds diagnostics; that's expected.
  RAW=$(cd "$ROOT" && ast-grep "${sg_args[@]}" 2>/dev/null || true)
  # Tag every ast-grep record with backend so reports can group/filter.
  [[ -n "$RAW" ]] && RAW=$(printf '%s\n' "$RAW" | jq -c '. + {backend: "ast-grep"}')
fi

# ---------- alint scan (structural rules) ----------
# alint emits one {schema_version, summary, results[]} document. Normalise its
# per-violation records into the same shape ast-grep produces so all downstream
# logic (suppression / severity / output / report) works uniformly.
ALINT_RAW=""
if (( ! STRUCTURAL_ONLY )) && command -v alint >/dev/null 2>&1 && [[ -f "$ROOT/.alint.yml" ]]; then
  alint_json=$(cd "$ROOT" && alint check -f json -q 2>/dev/null || true)
  if [[ -n "$alint_json" ]]; then
    rule_jq=$([[ -n "$RULE_FILTER" ]] && echo "select(.ruleId | test(\"$RULE_FILTER\"))" || echo ".")
    ALINT_RAW=$(printf '%s' "$alint_json" | jq -c "
      .results[]
      | .id as \$rid | .level as \$lvl
      | .violations[]
      | {
          ruleId: \$rid,
          severity: (if \$lvl == \"hint\" then \"info\" else \$lvl end),
          message: .message,
          file: .path,
          range: {
            start:       { line: 0, column: 0 },
            end:         { line: 0, column: 0 },
            byteOffset:  { start: 0, end: 0 }
          },
          text: .path,
          lines: \"\",
          metadata: {},
          backend: \"alint\"
        }
      | $rule_jq
    ")
  fi
fi

# Merge ast-grep + alint NDJSON streams.
if [[ -n "$ALINT_RAW" ]]; then
  RAW=$([[ -n "$RAW" ]] && printf '%s\n%s\n' "$RAW" "$ALINT_RAW" || printf '%s\n' "$ALINT_RAW")
fi

# ---------- clang-tidy / libclang scan (type-aware rules) ----------
# Backend script handles its own toolchain availability checks and emits
# unified NDJSON directly (one record per finding, backend: clang-tidy).
TIDY_RAW=""
TIDY_SCRIPT="$ROOT/utils/lint/tidy/run_tidy.sh"
if (( WITH_TIDY )) && (( ! STRUCTURAL_ONLY )) && [[ -x "$TIDY_SCRIPT" ]]; then
  TIDY_RAW=$("$TIDY_SCRIPT" 2>/dev/null || true)
  # Apply --rule filter on the tidy stream (same shape as ast-grep filter).
  if [[ -n "$RULE_FILTER" && -n "$TIDY_RAW" ]]; then
    TIDY_RAW=$(printf '%s\n' "$TIDY_RAW" | jq -c "select(.ruleId | test(\"$RULE_FILTER\"))" 2>/dev/null || true)
  fi
fi

if [[ -n "$TIDY_RAW" ]]; then
  RAW=$([[ -n "$RAW" ]] && printf '%s\n%s\n' "$RAW" "$TIDY_RAW" || printf '%s\n' "$TIDY_RAW")
fi

# ---------- hybrid backend: unused-function check (Phase 4) ----------
# Stages 1+2 (ast-grep lexical analysis) — measured ~4 s on this codebase,
# so runs in both the fast `make lint` and `make lint-full`.
UNUSED_RAW=""
UNUSED_SCRIPT="$ROOT/utils/lint/dead-code/run_unused_function.sh"
if (( ! STRUCTURAL_ONLY )) && [[ -x "$UNUSED_SCRIPT" ]]; then
  UNUSED_RAW=$("$UNUSED_SCRIPT" 2>/dev/null || true)
  if [[ -n "$RULE_FILTER" && -n "$UNUSED_RAW" ]]; then
    UNUSED_RAW=$(printf '%s\n' "$UNUSED_RAW" | jq -c "select(.ruleId | test(\"$RULE_FILTER\"))" 2>/dev/null || true)
  fi
fi

if [[ -n "$UNUSED_RAW" ]]; then
  RAW=$([[ -n "$RAW" ]] && printf '%s\n%s\n' "$RAW" "$UNUSED_RAW" || printf '%s\n' "$UNUSED_RAW")
fi

# ---------- suppression filter (jq) ----------
# Drop records whose metadata.suppress marker (e.g. RAWALLOC_OK) appears in .lines.
SUPP_JQ='
  .metadata.suppress as $s
  | ($s == null) or (($s | tostring | length) == 0)
    or ((.lines // "") | contains($s) | not)
'
FILTERED=""
SUPPRESSED_COUNT=0
if [[ -n "$RAW" ]]; then
  FILTERED=$(printf '%s\n' "$RAW" | jq -c "select($SUPP_JQ)")
  SUPPRESSED_COUNT=$(printf '%s\n' "$RAW" | jq -c "select(($SUPP_JQ) | not)" | grep -c . || true)
fi
FILTERED_COUNT=0
[[ -n "$FILTERED" ]] && FILTERED_COUNT=$(printf '%s\n' "$FILTERED" | grep -c .)

# Count findings per severity. Only `error` severity fails the gate; `warning`
# and `info` are surfaced (and land in reports) but don't break the build, so
# new broad rules can ship without a fix-everything cliff.
count_sev() { [[ -z "$FILTERED" ]] && { echo 0; return; }; printf '%s\n' "$FILTERED" | jq -c "select(.severity == \"$1\")" | grep -c . || true; }
ERROR_COUNT=$(count_sev error)
WARN_COUNT=$(count_sev warning)
INFO_COUNT=$(count_sev info)

# ---------- output ----------
RED=""; YELLOW=""; GREEN=""; DIM=""; RESET=""
if [[ -t 1 ]]; then RED=$'\e[31m'; YELLOW=$'\e[33m'; GREEN=$'\e[32m'; DIM=$'\e[2m'; RESET=$'\e[0m'; fi

ag_fail=0
(( ERROR_COUNT > 0 )) && ag_fail=1

if (( FILTERED_COUNT > 0 )); then
  case "$FORMAT" in
    github)
      printf '%s\n' "$FILTERED" | jq -r '
        "::\(.severity) file=\(.file),line=\(.range.start.line+1),col=\(.range.start.column+1)"
        + "::[\(.ruleId)] \(.message // "" | gsub("\\s+"; " "))"'
      ;;
    *)
      # group by rule, print header (colored by severity) + indented findings
      printf '%s\n' "$FILTERED" | jq -sr \
        --arg red "$RED" --arg yellow "$YELLOW" --arg dim "$DIM" --arg reset "$RESET" '
        def icon: if . == "error" then "❌" elif . == "warning" then "⚠️ " else "ℹ️ " end;
        def color: if . == "error" then $red elif . == "warning" then $yellow else $dim end;
        group_by(.ruleId)[]
        | "\(.[0].severity | color)\(.[0].severity | icon) \(.[0].ruleId) [\(.[0].severity)]: \(length) finding(s)\($reset)\n"
          + (map("   \(.file):\(.range.start.line+1)  \($dim)\(.lines // .text // "" | sub("^\\s+";""))\($reset)")
             | join("\n"))'
      ;;
  esac
elif (( ! STRUCTURAL_ONLY )); then
  scope=$([[ -n "$RULE_FILTER" ]] && echo "rule '$RULE_FILTER'" || echo "all rules")
  extra=""
  (( SUPPRESSED_COUNT > 0 )) && extra=" ($SUPPRESSED_COUNT suppressed)"
  printf '%s✅ ast-grep: no violations for %s%s%s\n' "$GREEN" "$scope" "$extra" "$RESET"
fi

# ---------- structural dispatch ----------
struct_fail=0
run_struct=0
(( ! NO_STRUCTURAL )) && [[ -z "$RULE_FILTER" ]] && run_struct=1
(( STRUCTURAL_ONLY )) && run_struct=1
if (( run_struct )); then
  for entry in "${STRUCTURAL_CHECKS[@]}"; do
    id="${entry%%:*}"; cmd="${entry#*:}"
    if ! $cmd; then
      struct_fail=1
      printf '%s❌ structural:%s failed%s\n' "$RED" "$id" "$RESET" >&2
    fi
  done
fi

# ---------- report ----------
write_report() {
  mkdir -p "$REPORT_DIR"
  # auto-increment: first free Report_NNN.json index
  local n=1
  while [[ -e "$REPORT_DIR/Report_$(printf '%03d' $n).json" ]]; do n=$((n+1)); done
  local stem="$REPORT_DIR/Report_$(printf '%03d' $n)"

  local now branch commit ag_version
  now=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
  branch=$(git -C "$ROOT" branch --show-current 2>/dev/null || echo "")
  commit=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo "")
  ag_version=$(ast-grep --version 2>/dev/null | awk '{print $2}')

  # ---- JSON: enriched, post-suppression report (the canonical agent input) ----
  printf '%s\n' "$FILTERED" \
    | jq -s --arg ts "$now" --arg branch "$branch" --arg commit "$commit" \
           --arg ag "$ag_version" --arg rule_filter "$RULE_FILTER" \
           --argjson suppressed "$SUPPRESSED_COUNT" \
           --argjson scan_roots "$(printf '%s\n' "${SCAN_ROOTS[@]}" | jq -R . | jq -sc .)" '
      def normmsg: (. // "" | gsub("\\s+"; " ") | sub("^ +"; "") | sub(" +$"; ""));
      {
        meta: {
          generated: $ts,
          branch: $branch,
          commit: $commit,
          ast_grep_version: $ag,
          rule_filter: $rule_filter,
          scan_roots: $scan_roots,
          total_findings: length,
          suppressed_inline: $suppressed
        },
        counts_by_rule: (group_by(.ruleId)
          | map({rule: .[0].ruleId, severity: .[0].severity,
                 message: (.[0].message | normmsg), count: length})
          | sort_by(-.count)),
        counts_by_file: (group_by(.file)
          | map({file: .[0].file, count: length})
          | sort_by(-.count)),
        counts_by_backend: (group_by(.backend // "ast-grep")
          | map({backend: (.[0].backend // "ast-grep"), count: length})
          | sort_by(-.count)),
        findings: (map({
          rule_id: .ruleId,
          backend: (.backend // "ast-grep"),
          severity: .severity,
          message: (.message | normmsg),
          file: .file,
          line: (.range.start.line + 1),
          column: (.range.start.column + 1),
          end_line: (.range.end.line + 1),
          end_column: (.range.end.column + 1),
          byte_start: .range.byteOffset.start,
          byte_end: .range.byteOffset.end,
          text: .text,
          source_line: (.lines // "" | sub("\\s+$"; ""))
        }) | sort_by(.backend, .rule_id, .file, .line))
      }' > "$stem.json"

  # ---- TSV: flat, one finding per row (greppable/awkable) ----
  # Source-of-truth ordering is taken from the sorted findings array in the JSON,
  # so md / json / tsv all enumerate findings in the same (rule, file, line) order.
  {
    printf 'backend\trule_id\tseverity\tfile\tline\tcolumn\ttext\tmessage\n'
    jq -r '.findings[] | [
        .backend,
        .rule_id,
        .severity,
        .file,
        (.line | tostring),
        (.column | tostring),
        (.text | gsub("[\t\n]"; " ")),
        (.message | gsub("[\t\n]"; " "))
      ] | @tsv' "$stem.json"
  } > "$stem.tsv"

  # ---- Markdown: human summary ----
  {
    echo "# Lambda Lint Report — $(basename "$stem")"
    echo
    echo "| Field | Value |"
    echo "|-------|-------|"
    echo "| Generated | \`$now\` |"
    echo "| Branch | \`${branch:-?}\` @ \`${commit:-?}\` |"
    echo "| ast-grep | \`$ag_version\` |"
    [[ -n "$RULE_FILTER" ]] && echo "| Rule filter | \`$RULE_FILTER\` |"
    echo "| Findings (unsuppressed) | **$FILTERED_COUNT** |"
    echo "| Inline-suppressed | $SUPPRESSED_COUNT |"
    echo
    if (( FILTERED_COUNT == 0 )); then
      echo "## ✅ Clean"
      echo
      echo "No unsuppressed findings."
    else
      echo "## Summary by rule"
      echo
      echo "| Rule | Severity | Count |"
      echo "|------|----------|------:|"
      jq -r '.counts_by_rule[] | "| `\(.rule)` | \(.severity) | \(.count) |"' "$stem.json"
      echo
      echo "## Top files"
      echo
      echo "| File | Findings |"
      echo "|------|---------:|"
      jq -r '.counts_by_file[:15][] | "| `\(.file)` | \(.count) |"' "$stem.json"
      echo
      echo "## Per-rule details"
      jq -r '.counts_by_rule[].rule' "$stem.json" | while read -r r; do
        echo
        echo "### \`$r\`"
        echo
        msg=$(jq -r --arg r "$r" '.counts_by_rule[] | select(.rule == $r) | .message' "$stem.json")
        echo "> $msg"
        echo
        # show up to 20 findings, then a "+N more" footer
        n=$(jq --arg r "$r" '[.findings[] | select(.rule_id == $r)] | length' "$stem.json")
        echo "**$n location(s)** (showing first $((n < 20 ? n : 20))):"
        echo
        jq -r --arg r "$r" '[.findings[] | select(.rule_id == $r)] | .[0:20][]
          | "- `\(.file):\(.line)` — `\(.source_line | gsub("`"; "\\`"))`"' "$stem.json"
        if (( n > 20 )); then echo; echo "_…and $((n - 20)) more — see \`$(basename "$stem").json\` / \`.tsv\`._"; fi
      done
    fi
    echo
    echo "---"
    echo
    echo "**Machine-readable companions** (same stem):"
    echo "- \`$(basename "$stem").json\` — full structured findings + meta + counts"
    echo "- \`$(basename "$stem").tsv\` — flat one-row-per-finding for batch processing"
  } > "$stem.md"

  printf '%s📄 Lint report: %s.{md,json,tsv}%s\n' "$GREEN" "${stem#$ROOT/}" "$RESET"
}

if (( WRITE_REPORT )); then write_report; fi

(( ag_fail || struct_fail )) && exit 1 || exit 0
