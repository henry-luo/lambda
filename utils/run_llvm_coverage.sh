#!/usr/bin/env bash

# run an instrumented test lane, restore the normal Lambda host, and report coverage.
set -Eeuo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <native|js> <coverage-dir> <binary-dir>" >&2
    exit 2
fi

mode=$1
coverage_dir=$2
binary_dir=$3
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

profile_pattern="$coverage_dir/%p.profraw"
backup_host="$coverage_dir/.lambda.exe.backup"
had_host=0
host_swapped=0
test_status=0

mkdir -p "$coverage_dir"
rm -f "$coverage_dir"/*.profraw "$backup_host"

if [ -e lambda.exe ]; then
    cp -p lambda.exe "$backup_host"
    had_host=1
fi
cp -p "$binary_dir/lambda.exe" lambda.exe
host_swapped=1

restore_host() {
    if [ "$host_swapped" -eq 0 ]; then
        return
    fi
    if [ "$had_host" -eq 1 ]; then
        mv -f "$backup_host" lambda.exe
    else
        rm -f lambda.exe
    fi
    host_swapped=0
}
trap restore_host EXIT HUP INT TERM

if [ "$mode" = native ]; then
    LAMBDA_TEST_BIN_DIR="$binary_dir" \
    LLVM_PROFILE_FILE="$profile_pattern" \
    node test/test_run.js || test_status=$?
elif [ "$mode" = js ]; then
    LLVM_PROFILE_FILE="$profile_pattern" \
    "$binary_dir/test_js_gtest.exe" \
    --gtest_color=no \
    --gtest_output="json:$coverage_dir/test_js_gtest_results.json" || test_status=$?

    js_test262_status=0
    LLVM_PROFILE_FILE="$profile_pattern" \
    "$binary_dir/test_js_test262_gtest.exe" \
    --batch-only --run-async \
    --async-list=test/js262/test262_baseline.txt \
    --gtest_color=no \
    --gtest_output="json:$coverage_dir/test_js_test262_results.json" || js_test262_status=$?
    if [ "$js_test262_status" -ne 0 ] && [ "$test_status" -eq 0 ]; then
        test_status=$js_test262_status
    fi
else
    echo "unknown coverage mode: $mode" >&2
    exit 2
fi

restore_host
trap - EXIT HUP INT TERM

report_status=0
bash utils/llvm_coverage_report.sh "$coverage_dir" "$binary_dir" || report_status=$?

if [ "$test_status" -ne 0 ]; then
    exit "$test_status"
fi
exit "$report_status"
