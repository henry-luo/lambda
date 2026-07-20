#!/usr/bin/env bash
# Run the Stage-4B "JS editor under Radiant" event-driven fixtures
# (test/ui/editor4b/*.json) against the built single-file editor page using the
# headless `lambda.exe view --event-file` testdriver. Reports per-fixture
# pass/fail and a summary. Run from the repo root.
#
#   bash test/editor-js/tools/run-radiant-fixtures.sh
#
# Prereqs: `make build` (lambda.exe) and `npm --prefix test/editor-js run
# build:page-dom` (test/html/editor-dom.html).
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1

EXE=./lambda.exe
DIR=test/ui/editor4b
pass=0; fail=0; failed=()

for j in "$DIR"/*.json; do
  : > log.txt 2>/dev/null || true
  # Each fixture names its own page in the JSON "html" field.
  page=$(grep -o '"html"[^,]*' "$j" | head -1 | sed 's/.*: *"//;s/"//')
  out=$(timeout 90 "$EXE" view "$page" --event-file "$j" --headless --no-log 2>&1)
  line=$(printf '%s\n' "$out" | grep -iE "Assertions:" | tail -1)
  crash=$(printf '%s\n' "$out" | grep -ic "AddressSanitizer")
  name=$(basename "$j")
  if printf '%s' "$line" | grep -q "0 failed" && [ "$crash" = "0" ]; then
    echo "PASS  $name -$line"
    pass=$((pass+1))
  else
    echo "FAIL  $name -${line:- (no assertions / crash=$crash)}"
    fail=$((fail+1)); failed+=("$name")
  fi
done

echo "----------------------------------------"
echo "editor4b radiant fixtures: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
