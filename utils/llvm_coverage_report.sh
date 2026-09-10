#!/usr/bin/env bash

# merge LLVM profiles and emit text, JSON, and HTML coverage artifacts.
set -Eeuo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <coverage-dir> <binary-dir>" >&2
    exit 2
fi

coverage_dir=$1
binary_dir=$2
mkdir -p "$coverage_dir"
html_dir="$coverage_dir/html"
profdata="$coverage_dir/coverage.profdata"
report_txt="$coverage_dir/report.txt"
export_json="$coverage_dir/coverage.json"
objects_txt="$coverage_dir/objects.txt"
metadata_txt="$coverage_dir/metadata.txt"

resolve_tool() {
    local tool_name=$1
    if command -v "$tool_name" >/dev/null 2>&1; then
        command -v "$tool_name"
    elif command -v xcrun >/dev/null 2>&1; then
        xcrun --find "$tool_name" 2>/dev/null || true
    fi
}

llvm_cov=${LLVM_COV:-$(resolve_tool llvm-cov)}
llvm_profdata=${LLVM_PROFDATA:-$(resolve_tool llvm-profdata)}

if [ -z "$llvm_cov" ] || [ ! -x "$llvm_cov" ]; then
    echo "llvm-cov was not found" >&2
    exit 1
fi
if [ -z "$llvm_profdata" ] || [ ! -x "$llvm_profdata" ]; then
    echo "llvm-profdata was not found" >&2
    exit 1
fi

shopt -s nullglob
profiles=("$coverage_dir"/*.profraw)
if [ "${#profiles[@]}" -eq 0 ]; then
    echo "No LLVM profile data found in $coverage_dir" >&2
    exit 1
fi

objects=()
while IFS= read -r object; do
    objects+=("$object")
done < <(find "$binary_dir" -type f -perm -111 -print | sort)
if [ "${#objects[@]}" -eq 0 ]; then
    echo "No executable coverage objects found in $binary_dir" >&2
    exit 1
fi

object_args=()
for object in "${objects[@]:1}"; do
    object_args+=("-object" "$object")
done

rm -rf "$html_dir"
mkdir -p "$html_dir"
"$llvm_profdata" merge -sparse "${profiles[@]}" -o "$profdata"
printf '%s\n' "${objects[@]}" > "$objects_txt"
{
    echo "llvm-cov=$llvm_cov"
    echo "llvm-profdata=$llvm_profdata"
    echo "binary-dir=$binary_dir"
    echo "profile-count=${#profiles[@]}"
    echo "object-count=${#objects[@]}"
    echo "profile-data=$profdata"
} > "$metadata_txt"

"$llvm_cov" report "${objects[0]}" "${object_args[@]}" \
    -instr-profile="$profdata" > "$report_txt"
"$llvm_cov" export "${objects[0]}" "${object_args[@]}" \
    -instr-profile="$profdata" -format=text > "$export_json"
"$llvm_cov" show "${objects[0]}" "${object_args[@]}" \
    -instr-profile="$profdata" -format=html \
    -show-line-counts-or-regions -output-dir="$html_dir"

echo "HTML report: $html_dir/index.html"
echo "Text report: $report_txt"
echo "JSON metadata: $export_json"
echo "Profile data: $profdata"
