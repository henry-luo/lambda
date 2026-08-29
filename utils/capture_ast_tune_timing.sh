#!/bin/sh
set -eu

suite=""
label=""
warmups=1
runs=5
while [ "$#" -gt 0 ]; do
    case "$1" in
        --suite) suite=$2; shift 2 ;;
        --label) label=$2; shift 2 ;;
        --warmups) warmups=$2; shift 2 ;;
        --runs) runs=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

case "$suite" in
    lambda) test_exe=./test/test_lambda_gtest.exe; test_filter='AutoDiscovered/LambdaScriptTest.*' ;;
    js) test_exe=./test/test_js_gtest.exe; test_filter='JavaScriptTests/JsFileTest.*' ;;
    *) echo "--suite must be lambda or js" >&2; exit 2 ;;
esac
[ -n "$label" ] || { echo "--label is required" >&2; exit 2; }
[ -x "$test_exe" ] || { echo "missing $test_exe; run make build-test" >&2; exit 1; }
# JS timing matches the production baseline lane; Lambda keeps its full corpus.
test_options=""; [ "$suite" = js ] && test_options="--baseline"

root="./temp/ast_tune/$label/$suite"
run_dir="$root/runs"
mkdir -p "$run_dir"
rm -f "$root/manifest.tsv" "$root/metadata.txt"

git_commit=$(git rev-parse HEAD 2>/dev/null || echo unknown)
git_dirty=$(test -n "$(git status --porcelain 2>/dev/null)" && echo dirty || echo clean)
{
    echo "schema_version=1"
    echo "suite=$suite"
    echo "label=$label"
    echo "commit=$git_commit"
    echo "dirty=$git_dirty"
    echo "warmups=$warmups"
    echo "runs=$runs"
    echo "host=$(uname -a)"
} > "$root/metadata.txt"

total=$((warmups + runs))
i=0
while [ "$i" -lt "$total" ]; do
    run_id="${label}_${suite}_${i}"
    tsv="$root/current.tsv"
    log="$root/run_${i}.log"
    rm -f "$tsv"
    echo "capture suite=$suite label=$label run=$i/$total"
    set +e
    LAMBDA_COMPILER_TIMING=1 \
    AST_TUNE_BATCH_WORKERS=4 \
    AST_TUNE_BATCH_CHUNK=50 \
    AST_TUNE_RUN_ID="$run_id" \
    AST_TUNE_TIMING_TSV="$tsv" \
    "$test_exe" $test_options --gtest_filter="$test_filter" > "$log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ] && [ "${AST_TUNE_ALLOW_TEST_FAILURES:-0}" != "1" ]; then
        echo "test run failed: $log" >&2
        exit "$status"
    fi
    [ -s "$tsv" ] || { echo "missing timing TSV: $tsv" >&2; exit 1; }
    rows=$(awk 'NR > 1 { n++ } END { print n + 0 }' "$tsv")
    [ "$rows" -gt 0 ] || { echo "empty timing TSV: $tsv" >&2; exit 1; }
    expected=$($test_exe $test_options --gtest_list_tests --gtest_filter="$test_filter" | awk '/# GetParam\(\)/ { n++ } END { print n + 0 }')
    [ "$rows" -eq "$expected" ] || { echo "incomplete $suite timing TSV: $rows/$expected rows" >&2; exit 1; }
    # A timing capture must contain one complete compiler+volume record for
    # every selected test.  Missing records are never treated as cache hits:
    # that would silently turn a crashed/retried sample into a speedup.
    bad=$(awk -F '\t' 'NR > 1 && ($7 != "cold" || $20 == "" || $25 == "") { n++ } END { print n + 0 }' "$tsv")
    [ "$bad" -eq 0 ] || { echo "invalid or incomplete compiler records in $tsv: $bad" >&2; exit 1; }
    if [ "$i" -ge "$warmups" ]; then
        cp "$tsv" "$run_dir/run_$((i - warmups)).tsv"
    fi
    i=$((i + 1))
done

cp "$run_dir/run_0.tsv" "$root/manifest.tsv"
manifest=$(awk -F '\t' 'NR > 1 { print $4 }' "$root/manifest.tsv" | sort)
printf '%s\n' "$manifest" > "$root/manifest.sample_ids"
summary="$root/summary.md"
{
    echo "# AST tune capture: $suite / $label"
    echo
    echo "| metric | value |"
    echo "|---|---:|"
    awk -F '\t' '
        NR > 1 { n++; total += $20; parse += $9; ast += $10; bind += $11;
            validate += $12; analysis += $14; mir += $16; link += $19 }
        END { printf "| samples | %d |\n| compiler_total_us | %d |\n| parse_us | %d |\n| ast_build_us | %d |\n| bind_us | %d |\n| validate_us | %d |\n| analysis_us | %d |\n| mir_lower_us | %d |\n| link_us | %d |\n", n, total, parse, ast, bind, validate, analysis, mir, link }
    ' "$run_dir/run_0.tsv"
    echo
    echo "## Compiler-time distribution (us)"
    awk -F '\t' 'NR > 1 { print $20 }' "$run_dir/run_0.tsv" | sort -n | awk '
        { v[NR] = $1 }
        END {
            if (NR == 0) exit 1;
            p50 = (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2;
            p95 = v[int((NR * 95 + 99) / 100)];
            printf "p50=%d\np95=%d\n", p50, p95;
        }
    '
    echo
    echo "## Slowest samples"
    echo "| compiler_us | test_name | sample_id | status |"
    echo "|---:|---|---|---:|"
    awk -F '\t' 'NR > 1 { printf "%s\t%s\t%s\t%s\n", $20, $5, $4, $6 }' \
        "$run_dir/run_0.tsv" | sort -nr | head -10 | \
        awk -F '\t' '{ printf "| %s | %s | %s | %s |\n", $1, $2, $3, $4 }'
} > "$summary"
echo "captured suite=$suite label=$label rows=$(wc -l < "$root/manifest.sample_ids")"
echo "summary=$summary"
