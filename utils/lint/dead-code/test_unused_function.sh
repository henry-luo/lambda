#!/usr/bin/env bash
# Verify that the lexical gate reports a dead external definition without
# misclassifying a live external definition from the same translation unit.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CHECKER="$ROOT/utils/lint/dead-code/run_unused_function.sh"
FIXTURE="$ROOT/utils/lint/dead-code/fixtures/external_functions.cpp"

if ! OUTPUT=$("$CHECKER" "$FIXTURE"); then
  echo "unused-fn fixture: checker failed" >&2
  exit 1
fi

if ! printf '%s\n' "$OUTPUT" | jq -e 'select(.text == "dead_external_fixture")' >/dev/null; then
  echo "unused-fn fixture: dead external function was not reported" >&2
  exit 1
fi

if printf '%s\n' "$OUTPUT" | jq -e 'select(.text == "live_external_fixture")' >/dev/null; then
  echo "unused-fn fixture: live external function was reported" >&2
  exit 1
fi
