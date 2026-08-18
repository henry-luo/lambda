#!/bin/sh
# Regenerate the JS ANY-census baseline [Type_Infer TI3 / Impl IP6].
# Companion to refresh_census.sh: same reason vocabulary, JS corpus.
set -e
OUT="${1:-test/type_infer/any_census_js_baseline.tsv}"
EXE="${LAMBDA_EXE:-./lambda.exe}"
TMP="temp/any_census_js_raw.txt"
mkdir -p temp
: > "$TMP"
for f in test/js/*.js; do
    [ -f "$f" ] || continue
    "$EXE" js "$f" 2>&1 | grep -o 'any_census: .*(js)' >> "$TMP" || true
done
python3 - "$TMP" "$OUT" <<'PY'
import re, sys, collections
raw, out = sys.argv[1], sys.argv[2]
totals = collections.Counter(); files = 0
for line in open(raw):
    if 'any_census' not in line: continue
    files += 1
    for name, count in re.findall(r'(\w+)=(\d+)', line):
        totals[name] += int(count)
with open(out, 'w') as f:
    f.write("# JS ANY-census baseline [Type_Infer TI3]. Regenerate with\n")
    f.write("# `sh test/type_infer/refresh_js_census.sh`; commit the diff with the change.\n")
    f.write("reason\tcount\n")
    f.write(f"_files\t{files}\n")
    for name in sorted(totals):
        if name == 'total': continue
        f.write(f"{name}\t{totals[name]}\n")
    f.write(f"_total\t{totals['total']}\n")
print(f"js census: {files} files, {totals['total']} any-typed nodes -> {out}")
PY
