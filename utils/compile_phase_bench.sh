#!/usr/bin/env bash
# Compile-phase benchmark for the Lambda front end (vibe/Lambda_Design_Compile_Pipeline.md §4).
#
# Runs each corpus script under LAMBDA_PROFILE=1 in the requested tier with
# logging disabled and no execution (--dry-run), REPS times, and reports the
# minimum parse / ast (build+bind+validate+index) / plan / transpile phase
# times in milliseconds. Output is a TSV on stdout; imported modules are
# excluded so every row is the named script's own compile.
#
# usage: utils/compile_phase_bench.sh [tier] [reps] [corpus-file]
#   tier         interp (default) | auto | jit
#   reps         repetitions per script, default 3
#   corpus-file  one script path per line; default: the 20-script corpus below
set -u
TIER=${1:-interp}
REPS=${2:-3}
CORPUS=${3:-}
LAMBDA=${LAMBDA_EXE:-./lambda.exe}
PROFILE=temp/phase_profile.txt

default_corpus() {
cat <<'EOF'
test/lambda/editor/oracle_poc.ls
test/lambda/complex_iot_report_html.ls
test/lambda/editor/commands_basic.ls
test/lambda/wip/complex_iot_report.ls
test/lambda/type_pattern.ls
test/lambda/editor/input_intent_basic.ls
test/lambda/wip/healthcare_analytics.ls
test/lambda/ui/todo2.ls
test/lambda/graph/structurizr/reference/structurizr_json_adapter.ls
test/lambda/editor/paste_basic.ls
test/benchmark/awfy/json2.ls
test/benchmark/awfy/json.ls
test/benchmark/awfy/cd.ls
test/benchmark/awfy/havlak.ls
test/benchmark/awfy/deltablue.ls
test/benchmark/awfy/cd2_orig.ls
test/benchmark/awfy/deltablue2.ls
test/benchmark/awfy/havlak2.ls
test/benchmark/awfy/cd2.ls
test/benchmark/text/prettier_ast2.ls
EOF
}

mkdir -p temp
printf "script\tbytes\tparse_ms\tast_ms\tplan_ms\ttranspile_ms\n"
{ if [ -n "$CORPUS" ]; then cat "$CORPUS"; else default_corpus; fi; } | while read -r script; do
    [ -z "$script" ] && continue
    best_parse=""; best_ast=""; best_plan=""; best_tr=""
    for _ in $(seq "$REPS"); do
        rm -f "$PROFILE"
        LAMBDA_PROFILE=1 LAMBDA_TIER="$TIER" timeout 300 "$LAMBDA" --no-log --dry-run "$script" >/dev/null 2>&1
        # the profile file is rewritten per process; keep only this script's own row
        row=$(grep -v '^#' "$PROFILE" 2>/dev/null | awk -F'\t' -v s="$script" '$1==s || $1 ~ ("/" s "$")' | tail -1)
        [ -z "$row" ] && continue
        p=$(echo "$row" | cut -f2); a=$(echo "$row" | cut -f3); pl=$(echo "$row" | cut -f4); tr=$(echo "$row" | cut -f5)
        best_parse=$(awk -v a="$best_parse" -v b="$p" 'BEGIN{print (a==""||b+0<a+0)?b:a}')
        best_ast=$(awk -v a="$best_ast" -v b="$a" 'BEGIN{print (a==""||b+0<a+0)?b:a}')
        best_plan=$(awk -v a="$best_plan" -v b="$pl" 'BEGIN{print (a==""||b+0<a+0)?b:a}')
        best_tr=$(awk -v a="$best_tr" -v b="$tr" 'BEGIN{print (a==""||b+0<a+0)?b:a}')
    done
    printf "%s\t%d\t%s\t%s\t%s\t%s\n" "$script" "$(wc -c < "$script")" "${best_parse:-NA}" "${best_ast:-NA}" "${best_plan:-NA}" "${best_tr:-NA}"
done
