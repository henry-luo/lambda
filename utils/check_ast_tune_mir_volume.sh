#!/bin/sh
set -eu

script=${1:-}
[ -n "$script" ] || { echo "usage: $0 test/js/file.js" >&2; exit 2; }
[ -f "$script" ] || { echo "missing script: $script" >&2; exit 1; }
mkdir -p ./temp/ast_tune/mir_equivalence
dump="./temp/ast_tune/mir_equivalence/$(basename "$script").mir"
out="./temp/ast_tune/mir_equivalence/$(basename "$script").out"
rm -f "$dump" "$out"

LAMBDA_COMPILER_TIMING=1 LAMBDA_MIR_DUMP_PATH="$dump" \
    ./lambda.exe js "$script" > "$out" 2>/dev/null
reported=$(sed -n 's/.*MIR_VOLUME .*insns=\([0-9][0-9]*\).*/\1/p' "$out" | tail -1)
[ -n "$reported" ] || { echo "missing MIR_VOLUME record" >&2; exit 1; }
[ -s "$dump" ] || { echo "missing finalized MIR artifact" >&2; exit 1; }
artifact=$(awk 'BEGIN { FS="\t" }
    # Count executable instructions in function bodies. MIR imports and
    # module/function terminators are item metadata, not finalized MIR ops.
    /^\t/ { if ($2 != "local" && $2 != "forward" && $2 != "endfunc" && $2 != "import" && $2 != "endmodule") n++ }
    END { print n + 0 }' "$dump")
echo "MIR_VOLUME_EQUIVALENCE script=$script reported=$reported artifact=$artifact"
[ "$reported" -eq "$artifact" ] || {
    echo "finalized MIR count mismatch" >&2
    exit 1
}
