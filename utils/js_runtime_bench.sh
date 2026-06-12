#!/usr/bin/env bash
# Tune8 §1.1 microbench runner.
#
# Runs each bench_*.js in test/js_runtime_bench/ through the release lambda.exe
# and aggregates the ns/op results. Use to gate hot-path fold candidates by
# A/B-comparing before-fold vs after-fold builds.
#
# Usage:
#   utils/js_runtime_bench.sh              # run all benches, print TSV to stdout
#   utils/js_runtime_bench.sh OUTFILE      # also save to OUTFILE
#   utils/js_runtime_bench.sh A B          # compare two prior result TSVs

set -eu -o pipefail

BENCH_DIR="test/js_runtime_bench"
LAMBDA="./lambda.exe"

if [[ ! -f "$LAMBDA" ]]; then
    echo "missing $LAMBDA — build release first (make build-release-compile)" >&2
    exit 2
fi
if [[ ! -d "$BENCH_DIR" ]]; then
    echo "missing $BENCH_DIR" >&2
    exit 2
fi

# Compare-mode: two prior result files. Print delta in ns/op_best per bench.
if [[ $# -eq 2 && -f "$1" && -f "$2" ]]; then
    A="$1"; B="$2"
    echo -e "bench\tA_ns_best\tB_ns_best\tdelta_ns\tpct"
    paste \
        <(awk -F'\t' 'NR>1 && $1 != "name" {print $1 "\t" $5}' "$A" | sort) \
        <(awk -F'\t' 'NR>1 && $1 != "name" {print $1 "\t" $5}' "$B" | sort) \
        | awk -F'\t' '
            $1 != $3 { next }
            {
                d = $4 - $2;
                pct = ($2 > 0) ? (100.0 * d / $2) : 0;
                printf "%s\t%.2f\t%.2f\t%+.2f\t%+.2f%%\n", $1, $2, $4, d, pct;
            }
        '
    exit 0
fi

# Run-mode.
out_file="${1:-}"
results=()

run_bench() {
    local file="$1"
    local label
    label=$(basename "$file" .js)
    # the bench file's stdout is multi-line TSV (header + rows). Tag each
    # data row with the bench filename so multiple files can be merged.
    "$LAMBDA" js "$file" 2>&1 \
        | awk -F'\t' -v lbl="$label" '
            NR==1 && $1 == "name" { next }     # skip header from common harness
            $1 == "name" { next }              # skip headers from later runs
            NF >= 6 { print lbl "::" $1 "\t" $2 "\t" $3 "\t" $4 "\t" $5 "\t" $6 }
        '
}

{
    echo -e "name\titers\tbest_ms\tavg_ms\tns_per_op_best\tns_per_op_avg"
    for f in "$BENCH_DIR"/bench_*.js; do
        [[ -f "$f" ]] || continue
        run_bench "$f"
    done
} | tee "${out_file:-/dev/stdout}"
