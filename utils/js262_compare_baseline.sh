#!/usr/bin/env bash
# Tune8 Phase 0 §1.2: js262 baseline comparison gate
#
# Usage: test/js262/compare_baseline.sh <new_run_dir> [<baseline_run_dir>]
#
# Compares a new js262 release run against a frozen baseline. The gate (per
# Tune8 §1.2 and Q4) admits a change only if:
#   1. Pass count is identical to baseline.
#   2. Sum of per-test elapsed_us is at or below baseline.
#
# Per-test and per-suite regressions are permitted; only the aggregate matters.
# Prints a one-line summary on success; prints a diff and exits 1 on failure.

set -u
set -o pipefail

BASELINE_DEFAULT="test/js262/results/release_run_006"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <new_run_dir> [<baseline_run_dir>]" >&2
    echo "       baseline defaults to ${BASELINE_DEFAULT}" >&2
    exit 2
fi

NEW_DIR="$1"
BASE_DIR="${2:-${BASELINE_DEFAULT}}"

NEW_BASELINE="${NEW_DIR}/test262_baseline_at_run.txt"
NEW_TIMING="${NEW_DIR}/t262_timing_release.tsv"
BASE_BASELINE="${BASE_DIR}/test262_baseline_at_run.txt"
BASE_TIMING="${BASE_DIR}/t262_timing_release.tsv"

for f in "$NEW_BASELINE" "$NEW_TIMING" "$BASE_BASELINE" "$BASE_TIMING"; do
    if [[ ! -f "$f" ]]; then
        echo "FAIL: missing file: $f" >&2
        exit 2
    fi
done

# Pass count: lines not starting with '#' in the baseline-at-run file.
new_pass=$(grep -c '^[^#]' "$NEW_BASELINE" || true)
base_pass=$(grep -c '^[^#]' "$BASE_BASELINE" || true)

# Summed wall-clock from the timing TSV (column 3 = elapsed_us, header skipped).
new_sum_us=$(awk -F'\t' 'NR>1 {sum += $3} END {printf "%.0f", sum}' "$NEW_TIMING")
base_sum_us=$(awk -F'\t' 'NR>1 {sum += $3} END {printf "%.0f", sum}' "$BASE_TIMING")

pass_ok=1
time_ok=1

if [[ "$new_pass" -ne "$base_pass" ]]; then
    pass_ok=0
fi

# Aggregate gate: new sum must be <= baseline sum.
if [[ "$new_sum_us" -gt "$base_sum_us" ]]; then
    time_ok=0
fi

# Pretty deltas.
delta_pass=$((new_pass - base_pass))
delta_us=$((new_sum_us - base_sum_us))
new_sum_s=$(awk -v u="$new_sum_us" 'BEGIN {printf "%.3f", u/1e6}')
base_sum_s=$(awk -v u="$base_sum_us" 'BEGIN {printf "%.3f", u/1e6}')
delta_s=$(awk -v u="$delta_us" 'BEGIN {printf "%+.3f", u/1e6}')
pct=$(awk -v d="$delta_us" -v b="$base_sum_us" 'BEGIN {if (b>0) printf "%+.2f%%", 100.0*d/b; else printf "n/a"}')

printf "baseline: %s\n" "$BASE_DIR"
printf "new:      %s\n" "$NEW_DIR"
printf "pass count: %d  (baseline %d, Δ %+d)\n" "$new_pass" "$base_pass" "$delta_pass"
printf "summed wall-clock: %s s  (baseline %s s, Δ %s s, %s)\n" \
    "$new_sum_s" "$base_sum_s" "$delta_s" "$pct"

if [[ "$pass_ok" -eq 1 && "$time_ok" -eq 1 ]]; then
    echo "GATE: PASS"
    exit 0
fi

echo "GATE: FAIL"
if [[ "$pass_ok" -eq 0 ]]; then
    echo "  - pass count regressed: $new_pass vs baseline $base_pass" >&2
fi
if [[ "$time_ok" -eq 0 ]]; then
    echo "  - aggregate wall-clock regressed by $delta_s s ($pct)" >&2
fi
exit 1
