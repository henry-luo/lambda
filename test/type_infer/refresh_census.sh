#!/bin/sh
# Regenerate the ANY-census baseline [Type_Infer TI3 / Impl IP0.3].
#
# The baseline is the acceptance metric for every later inference slice: a
# phase proves its effect by the per-reason delta, not by reading the emitter.
# Run after any inference change and check the diff in with the change.
#
# Usage: sh test/type_infer/refresh_census.sh [output.tsv]
set -e
OUT="${1:-test/type_infer/any_census_baseline.tsv}"
EXE="${LAMBDA_EXE:-./lambda.exe}"
TMP="temp/any_census_raw.txt"
mkdir -p temp
: > "$TMP"

# Corpus: the functional Lambda test scripts plus the benchmark ports. Both are
# checked in and stable, so the baseline moves only when inference moves.
for f in test/lambda/*.ls test/benchmark/*/*.ls; do
    [ -f "$f" ] || continue
    "$EXE" --emit-ast-dump "$f" 2>/dev/null | grep -o '(any_census .*' >> "$TMP" || true
done

python3 - "$TMP" "$OUT" <<'PY'
import re, sys, collections
raw, out = sys.argv[1], sys.argv[2]
totals = collections.Counter()
files = 0
for line in open(raw):
    line = line.strip()
    if not line.startswith('(any_census'):
        continue
    files += 1
    for name, count in re.findall(r'\((\w+) (\d+)\)', line):
        totals[name] += int(count)
with open(out, 'w') as f:
    f.write("# ANY-census baseline [Type_Infer TI3]. Regenerate with\n")
    f.write("# `sh test/type_infer/refresh_census.sh`; commit the diff with the change.\n")
    f.write("reason\tcount\n")
    f.write(f"_scripts\t{files}\n")
    for name in sorted(totals):
        if name == 'total':
            continue
        f.write(f"{name}\t{totals[name]}\n")
    f.write(f"_total\t{totals['total']}\n")
print(f"census baseline: {files} scripts, {totals['total']} any-typed nodes -> {out}")
PY
