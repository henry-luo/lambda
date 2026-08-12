#!/bin/sh
set -eu

script=${1:-test/lambda/simple_expr.ls}
[ -f "$script" ] || { echo "missing script: $script" >&2; exit 1; }

dir="./temp/ast_tune/instrumentation_equivalence"
mkdir -p "$dir"
plain="$dir/plain.out"
timed="$dir/timed.out"
plain_clean="$dir/plain.clean"
timed_clean="$dir/timed.clean"

LAMBDA_COMPILER_TIMING=0 ./lambda.exe "$script" > "$plain" 2>/dev/null
LAMBDA_COMPILER_TIMING=1 ./lambda.exe "$script" > "$timed" 2>/dev/null

# Control records are observability-only; remove them before comparing the
# user-visible result so timing cannot silently alter semantics or formatting.
grep -v 'COMPILER_TIMING\|MIR_VOLUME' "$plain" > "$plain_clean" || true
grep -v 'COMPILER_TIMING\|MIR_VOLUME' "$timed" > "$timed_clean" || true
cmp -s "$plain_clean" "$timed_clean" || {
    echo "instrumentation changed output for $script" >&2
    exit 1
}
echo "AST_TUNE_INSTRUMENTATION_EQUIVALENCE PASS script=$script"
