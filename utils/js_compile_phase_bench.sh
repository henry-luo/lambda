#!/usr/bin/env bash
# Compile-phase benchmark for the LambdaJS pipeline
# (vibe/Lambda_Design_Compiling_Pipeline_JS.md §4).
#
# Runs each corpus script with JS_TRANSPILE_TIMING=1 and logging disabled under
# the requested execution backend, REPS times, and reports the minimum named
# front-end, analysis/lowering, and link times in milliseconds,
# plus the collected function and MIR instruction counts. A script that does
# not finish within TIMEOUT seconds is reported as TIMEOUT. The corpus includes
# source files that need browser/CommonJS hosts, so a completed process's exit
# status is reported separately and does not discard its compiler timing. Output
# is a TSV.
#
# usage: utils/js_compile_phase_bench.sh [backend] [reps] [corpus-file]
#   backend      mir (explicit MIR lane) | ast | auto (the shipped default)
#   reps         repetitions per script, default 3
#   corpus-file  one script path per line; default: the 20-script corpus below
# env: LAMBDA_EXE (default ./lambda.exe), TIMEOUT seconds (default 120)
set -u
set -o pipefail
BACKEND=${1:-mir}
REPS=${2:-3}
CORPUS=${3:-}
LAMBDA=${LAMBDA_EXE:-./lambda.exe}
TIMEOUT=${TIMEOUT:-120}

default_corpus() {
cat <<'EOF'
ref/are-we-fast-yet/benchmarks/JavaScript/richards.js
ref/are-we-fast-yet/benchmarks/JavaScript/deltablue.js
ref/are-we-fast-yet/benchmarks/JavaScript/havlak.js
ref/are-we-fast-yet/benchmarks/JavaScript/cd.js
ref/are-we-fast-yet/benchmarks/JavaScript/nbody.js
ref/are-we-fast-yet/benchmarks/JavaScript/bounce.js
ref/are-we-fast-yet/benchmarks/JavaScript/json.js
ref/are-we-fast-yet/benchmarks/JavaScript/storage.js
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
    mir) BACKEND_ENV="JS_EXECUTION_BACKEND=mir" ;;
    ast|auto) BACKEND_ENV="JS_EXECUTION_BACKEND=$BACKEND" ;;
    *) echo "unknown backend: $BACKEND" >&2; exit 2 ;;
esac

min_of() { awk -v a="$1" -v b="$2" 'BEGIN{print (a==""||b+0<a+0)?b:a}'; }
timing_field() {
    printf "%s\n" "$1" | sed -nE "s/.* $2_ms=([0-9.]+).*/\\1/p" | tail -1
}
timing_sum() {
    total=0
    for field in "$@"; do
        value=$(timing_field "$line" "$field")
        total=$(awk -v a="$total" -v b="${value:-0}" 'BEGIN { printf "%.3f", a + b }')
    done
    printf "%s" "$total"
}

printf "script\tbytes\tbackend\tfrontend_ms\tmir_ms\tlink_ms\tfunctions\tmir_insns\texit_status\n"
{ if [ -n "$CORPUS" ]; then cat "$CORPUS"; else default_corpus; fi; } | while read -r script; do
    [ -z "$script" ] && continue
    fe=""; mir=""; link=""; fns=""; insns=""; status=""; exit_status=""
    for _ in $(seq "$REPS"); do
        out=$(env $BACKEND_ENV JS_TRANSPILE_TIMING=1 timeout "$TIMEOUT" "$LAMBDA" js --no-log "$script" 2>&1 | tr -d '\001')
        run_status=$?
        if [ "$run_status" -eq 124 ]; then
            status="TIMEOUT"
            continue
        fi
        line=$(printf "%s\n" "$out" | grep -a "^JS_TRANSPILE_TIMING" | tail -1)
        if [ -z "$line" ]; then status="ERROR($run_status:no-timing)"; continue; fi
        exit_status="$run_status"
        p=$(timing_sum parse_build bind validate index)
        m=$(timing_sum collect captures env_layout infer forward_declare mir_lower finalize prelink)
        l=$(timing_field "$line" link)
        fe=$(min_of "$fe" "$p"); mir=$(min_of "$mir" "$m"); link=$(min_of "$link" "$l")
        vol=$(printf "%s\n" "$out" | grep -a "^JS_MIR_VOLUME" | tail -1)
        [ -n "$vol" ] && fns=$(echo "$vol" | sed -E 's/.*functions=([0-9]+).*/\1/') && insns=$(echo "$vol" | sed -E 's/.*mir_insns=([0-9]+).*/\1/')
    done
    if [ -z "$fe" ]; then
        printf "%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$script" "$(wc -c < "$script")" "$BACKEND" "$status" "$status" "$status" "" "" ""
    else
        printf "%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$script" "$(wc -c < "$script")" "$BACKEND" "$fe" "$mir" "$link" "${fns:-}" "${insns:-}" "$exit_status"
    fi
done
