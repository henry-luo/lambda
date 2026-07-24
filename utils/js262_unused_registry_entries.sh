#!/usr/bin/env bash
# Tune8 Phase 0 §1.3 follow-up: identify js_* registry entries that telemetry
# observed zero JIT emissions for. These are the candidates for Phase 3
# deletion from the JIT import table (they stay as C symbols, just no
# {"name", FPTR(name)} row).
#
# Usage:
#   1. Run a telemetry-enabled build:
#        make clean-all
#        CXXFLAGS="-DJS_MIR_EMIT_TELEMETRY" make build-release-compile
#   2. Run the full test262 sweep:
#        make test262-baseline
#      (or test262-full for the broader sweep)
#   3. Each spawned lambda.exe writes ./temp/jit_emit_stats/<pid>.tsv on exit.
#   4. Run this script:
#        utils/js262_unused_registry_entries.sh
#      (defaults to ./temp/jit_emit_stats; pass a different path to override)
#
# Output: registry_name (one per line, to stdout) of every js_* entry that
# telemetry observed zero emissions for. Pipe into review.

set -eu -o pipefail

STATS="${1:-./temp/jit_emit_stats}"
REGISTRY="lambda/runtime/sys_func_registry.c"

if [[ ! -e "$STATS" ]]; then
    echo "missing telemetry data: $STATS" >&2
    echo "did you build with -DJS_MIR_EMIT_TELEMETRY and run the engine?" >&2
    exit 2
fi
if [[ ! -f "$REGISTRY" ]]; then
    echo "missing registry: $REGISTRY (run from project root)" >&2
    exit 2
fi

# Accept either a single TSV file or a directory of <pid>.tsv files.
if [[ -d "$STATS" ]]; then
    n_files=$(ls "$STATS"/*.tsv 2>/dev/null | wc -l | tr -d ' ')
    if [[ "$n_files" -eq 0 ]]; then
        echo "no per-PID .tsv files under $STATS" >&2
        exit 2
    fi
    echo "# aggregating $n_files per-PID telemetry files from $STATS" >&2
    # Sum counts per name across all per-PID TSVs.
    cat "$STATS"/*.tsv \
        | awk -F'\t' 'NR>1 || $1!="name" {if ($1 ~ /^js_/) tot[$1] += $2+0} END {for (n in tot) print n "\t" tot[n]}' \
        > /tmp/_t8_aggregated.tsv
else
    awk -F'\t' 'NR>1 && $1 ~ /^js_/ {print $1 "\t" $2}' "$STATS" > /tmp/_t8_aggregated.tsv
fi

# All js_* entry names registered in sys_func_defs[] / runtime import table.
awk '/^    \{"js_/ {gsub(/[",{}]/,""); print $1}' "$REGISTRY" | sort -u > /tmp/_t8_registered.txt

# Names with at least one observed emission.
awk -F'\t' '$2+0 > 0 {print $1}' /tmp/_t8_aggregated.tsv | sort -u > /tmp/_t8_emitted.txt

# Names registered but with zero observed emissions.
comm -23 /tmp/_t8_registered.txt /tmp/_t8_emitted.txt > /tmp/_t8_unused.txt

reg=$(wc -l < /tmp/_t8_registered.txt | tr -d ' ')
emit=$(wc -l < /tmp/_t8_emitted.txt | tr -d ' ')
unused=$(wc -l < /tmp/_t8_unused.txt | tr -d ' ')

printf "# registry entries: %s\n" "$reg" >&2
printf "# entries with telemetry hits: %s\n" "$emit" >&2
printf "# registered but never emitted: %s\n" "$unused" >&2
printf "# (these are the Phase 3 deletion candidates)\n" >&2

cat /tmp/_t8_unused.txt
