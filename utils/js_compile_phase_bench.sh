#!/usr/bin/env bash
# Compile-phase benchmark for the LambdaJS pipeline
# (vibe/Lambda_Design_Compiling_Pipeline_JS.md §4).
#
# Runs each corpus script with JS_TRANSPILE_TIMING=1 and logging disabled under
# the requested execution backend, REPS times, and reports the minimum
# front-end (parse+bind+validate+index, reported by the timer as parse_ms),
# MIR analysis+lowering, and link (native codegen) times in milliseconds,
# plus the collected function and MIR instruction counts. A script that does
# not finish within TIMEOUT seconds is reported as TIMEOUT. Output is a TSV.
#
# usage: utils/js_compile_phase_bench.sh [backend] [reps] [corpus-file]
#   backend      mir (default; the shipped default lane) | ast | auto
#   reps         repetitions per script, default 3
#   corpus-file  one script path per line; default: the 20-script corpus below
# env: LAMBDA_EXE (default ./lambda.exe), TIMEOUT seconds (default 120)
set -u
BACKEND=${1:-mir}
REPS=${2:-3}
CORPUS=${3:-}
LAMBDA=${LAMBDA_EXE:-./lambda.exe}
TIMEOUT=${TIMEOUT:-120}

default_corpus() {
cat <<'EOF'
ref/JetStream/Octane/richards.js
ref/JetStream/Octane/deltablue.js
ref/JetStream/Octane/raytrace.js
ref/JetStream/Octane/crypto.js
ref/JetStream/Octane/navier-stokes.js
ref/JetStream/Octane/splay.js
ref/JetStream/Octane/earley-boyer.js
ref/JetStream/Octane/code-first-load.js
test/js/lib_mustache.js
test/js/lib_immer.js
test/js/floating-ui.min.js
test/js/lib_popper.js
test/js/lib_fast_diff.js
test/js/alpine.min.js
test/js/htmx.min.js
test/js/bootstrap.min.js
test/js/lib_marked.js
test/js/lib_moment.js
test/js/hljs_highlight.js
test/js/lib_zod.js
EOF
}

case "$BACKEND" in
    mir) BACKEND_ENV="JS_EXECUTION_BACKEND=" ;;
    ast|auto) BACKEND_ENV="JS_EXECUTION_BACKEND=$BACKEND" ;;
    *) echo "unknown backend: $BACKEND" >&2; exit 2 ;;
esac

min_of() { awk -v a="$1" -v b="$2" 'BEGIN{print (a==""||b+0<a+0)?b:a}'; }

printf "script\tbytes\tbackend\tfrontend_ms\tmir_ms\tlink_ms\tfunctions\tmir_insns\n"
{ if [ -n "$CORPUS" ]; then cat "$CORPUS"; else default_corpus; fi; } | while read -r script; do
    [ -z "$script" ] && continue
    fe=""; mir=""; link=""; fns=""; insns=""; status=""
    for _ in $(seq "$REPS"); do
        out=$(env $BACKEND_ENV JS_TRANSPILE_TIMING=1 timeout "$TIMEOUT" "$LAMBDA" js --no-log "$script" 2>/dev/null | tr -d '\001')
        line=$(printf "%s\n" "$out" | grep -a "^JS_TRANSPILE_TIMING" | tail -1)
        if [ -z "$line" ]; then status="TIMEOUT"; continue; fi
        p=$(echo "$line" | sed -E 's/.*parse_ms=([0-9.]+).*/\1/')
        m=$(echo "$line" | sed -E 's/.*mir_ms=([0-9.]+).*/\1/')
        l=$(echo "$line" | sed -E 's/.*link_ms=([0-9.]+).*/\1/')
        fe=$(min_of "$fe" "$p"); mir=$(min_of "$mir" "$m"); link=$(min_of "$link" "$l")
        vol=$(printf "%s\n" "$out" | grep -a "^JS_MIR_VOLUME" | tail -1)
        [ -n "$vol" ] && fns=$(echo "$vol" | sed -E 's/.*functions=([0-9]+).*/\1/') && insns=$(echo "$vol" | sed -E 's/.*mir_insns=([0-9]+).*/\1/')
    done
    if [ -z "$fe" ]; then
        printf "%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n" "$script" "$(wc -c < "$script")" "$BACKEND" "$status" "$status" "$status" "" ""
    else
        printf "%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n" "$script" "$(wc -c < "$script")" "$BACKEND" "$fe" "$mir" "$link" "${fns:-}" "${insns:-}"
    fi
done
