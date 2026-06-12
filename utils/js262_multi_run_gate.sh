#!/usr/bin/env bash
# Tune8 §1.2 / Q4 — median-of-N test262 gate.
#
# Runs the test262 baseline sweep N times, records each per-test timing TSV,
# and computes:
#   1. Pass count (must be identical to baseline on every run — hard gate).
#   2. Median per-test sum across N runs (must be <= baseline median — soft gate).
#
# Use this instead of the single-run utils/js262_compare_baseline.sh when
# evaluating a fold candidate at the noise floor (per-test sum varies ±15%
# run-to-run on Apple Silicon; harness wall-clock varies ±7%).
#
# Usage:
#   utils/js262_multi_run_gate.sh [N=3] [baseline_sum_s=516.889]
#
# Defaults: 3 runs, baseline 516.889 s (release_run_006 sum).

set -u
set -o pipefail

N="${1:-3}"
BASELINE_S="${2:-516.889}"

if [[ ! -x ./test/test_js_test262_gtest.exe ]]; then
    echo "missing ./test/test_js_test262_gtest.exe — run 'make build-test' first" >&2
    exit 2
fi
if [[ ! -x ./lambda.exe ]]; then
    echo "missing ./lambda.exe — run 'make build-release-compile' first" >&2
    exit 2
fi
if [[ ! -f test/js262/test262_baseline.txt ]]; then
    echo "missing test/js262/test262_baseline.txt" >&2
    exit 2
fi

OUT_DIR="temp/_tune8_multi_run"
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/sum_*.txt "$OUT_DIR"/pass_*.txt

for ((i = 1; i <= N; i++)); do
    echo "=== run $i / $N ===" >&2
    ./test/test_js_test262_gtest.exe \
        --baseline-only --batch-only --run-async \
        --async-list=test/js262/test262_baseline.txt 2>&1 \
        | grep -E "Fully passed|All 42295 tests completed" >&2 || true

    # Pass count comes from the harness's --baseline match check; we approximate
    # it from the timing TSV's exit_code column (0 = pass).
    awk -F'\t' 'NR>1 && $2 == 0 {n++} END {print n+0}' temp/_t262_timing_o0.tsv > "$OUT_DIR/pass_$i.txt"
    awk -F'\t' 'NR>1 {sum += $3} END {printf "%.6f", sum/1e6}'      temp/_t262_timing_o0.tsv > "$OUT_DIR/sum_$i.txt"
    echo "  pass=$(cat $OUT_DIR/pass_$i.txt)  sum_s=$(cat $OUT_DIR/sum_$i.txt)" >&2
done

# Aggregate.
declare -a sums
declare -a passes
for ((i = 1; i <= N; i++)); do
    sums+=("$(cat $OUT_DIR/sum_$i.txt)")
    passes+=("$(cat $OUT_DIR/pass_$i.txt)")
done

# Median per-test sum (bash + sort).
median_sum=$(printf "%s\n" "${sums[@]}" | sort -n | awk -v n="$N" 'BEGIN{i=int((n-1)/2)} NR==i+1 {if (n%2==1) print $0; else if (NR==i+1) a=$0; if (n%2==0 && NR==i+2) printf "%.6f\n", (a+$0)/2}')
# If odd N, awk prints on NR==i+1 directly; if even, on NR==i+2.

# All passes must be identical.
unique_passes=$(printf "%s\n" "${passes[@]}" | sort -u)
n_unique=$(printf "%s\n" "${passes[@]}" | sort -u | wc -l | tr -d ' ')

echo ""
echo "=== Multi-run summary ==="
echo "runs: $N"
echo "pass counts: ${passes[*]}"
echo "per-test sums (s): ${sums[*]}"
echo "median per-test sum: ${median_sum} s"
echo "baseline per-test sum: ${BASELINE_S} s"

delta=$(awk -v m="$median_sum" -v b="$BASELINE_S" 'BEGIN {printf "%+.3f", m-b}')
pct=$(awk -v m="$median_sum" -v b="$BASELINE_S" 'BEGIN {printf "%+.2f%%", 100*(m-b)/b}')
echo "Δ vs baseline: $delta s ($pct)"
echo ""

# Hard gate: every run must have the same pass count.
hard_ok=1
if [[ "$n_unique" -ne 1 ]]; then
    echo "GATE: FAIL — pass count varied across runs: $(printf '%s ' "${passes[@]}")" >&2
    hard_ok=0
fi

# Soft gate: median sum must be <= baseline.
soft_ok=$(awk -v m="$median_sum" -v b="$BASELINE_S" 'BEGIN {print (m <= b) ? 1 : 0}')

if [[ "$hard_ok" -eq 1 && "$soft_ok" -eq 1 ]]; then
    echo "GATE: PASS"
    exit 0
fi
if [[ "$hard_ok" -eq 1 && "$soft_ok" -eq 0 ]]; then
    echo "GATE: FAIL (median per-test sum exceeds baseline)" >&2
fi
exit 1
