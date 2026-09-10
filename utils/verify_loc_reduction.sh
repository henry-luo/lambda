#!/usr/bin/env bash
# Verify that a clean-up phase actually reduced lines of code.
#
# Compares the total LOC of a file set between a git ref (default: HEAD)
# and the working tree. New helper files created by the phase MUST be
# listed too, so the total is honest (a phase that moves 300 lines into
# a new header saves nothing).
#
# Usage:
#   ./utils/verify_loc_reduction.sh [--ref <git-ref>] [--min-reduction N] <file> [file...]
#   ./utils/verify_loc_reduction.sh --ref master --min-reduction 1000 --files0-from manifest
#
# Exit code: 0 if total LOC strictly decreased, 1 otherwise.

set -euo pipefail

REF="HEAD"
MIN_REDUCTION=1
FILES0_FROM=""
FILES=()

while [ $# -gt 0 ]; do
    case "$1" in
        --ref)
            REF="$2"
            shift 2
            ;;
        --min-reduction)
            MIN_REDUCTION="$2"
            shift 2
            ;;
        --files0-from)
            FILES0_FROM="$2"
            shift 2
            ;;
        --)
            shift
            FILES+=("$@")
            break
            ;;
        *)
            FILES+=("$1")
            shift
            ;;
    esac
done

if [ -n "$FILES0_FROM" ]; then
    while IFS= read -r -d '' f; do
        FILES+=("$f")
    done < "$FILES0_FROM"
fi

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "usage: $0 [--ref <git-ref>] [--min-reduction N] [--files0-from manifest] <file> [file...]" >&2
    exit 2
fi

if ! [[ "$MIN_REDUCTION" =~ ^[0-9]+$ ]]; then
    echo "--min-reduction must be a non-negative integer" >&2
    exit 2
fi

# Count physical source lines that contain code. The small lexer deliberately
# handles strings/chars before comment markers so URLs and diagnostic strings
# are not mistaken for comments. Preprocessor lines count as code.
count_code_lines() {
    awk '
    BEGIN { in_block = 0; count = 0 }
    {
        line = $0
        code = ""
        quote = ""
        i = 1
        n = length(line)
        while (i <= n) {
            c = substr(line, i, 1)
            d = (i < n) ? substr(line, i + 1, 1) : ""
            if (in_block) {
                if (c == "*" && d == "/") { in_block = 0; i += 2 }
                else { i++ }
                continue
            }
            if (quote != "") {
                code = code c
                if (c == "\\" && i < n) { code = code substr(line, i + 1, 1); i += 2; continue }
                if (c == quote) quote = ""
                i++
                continue
            }
            if (c == "\"" || c == "\047") { quote = c; code = code c; i++; continue }
            if (c == "/" && d == "/") break
            if (c == "/" && d == "*") { in_block = 1; i += 2; continue }
            code = code c
            i++
        }
        gsub(/[[:space:]]/, "", code)
        if (length(code) > 0) count++
    }
    END { print count + 0 }
    '
}

git_code_lines() {
    local ref="$1"
    local file="$2"
    if git cat-file -e "$ref:$file" 2>/dev/null; then
        git show "$ref:$file" | count_code_lines
    else
        echo 0
    fi
}

working_code_lines() {
    local file="$1"
    if [ -f "$file" ]; then
        count_code_lines < "$file"
    else
        echo 0
    fi
}

total_old=0
total_new=0
code_old_total=0
code_new_total=0

printf "%-55s %8s %8s %8s %8s %8s %8s\n" "file" "before" "after" "delta" "code_old" "code_new" "code_delta"
printf "%-55s %8s %8s %8s %8s %8s %8s\n" "----" "------" "-----" "-----" "--------" "--------" "----------"

for f in "${FILES[@]}"; do
    # file may be new (not in ref) or deleted (not in working tree)
    if git cat-file -e "$REF:$f" 2>/dev/null; then
        old=$(git show "$REF:$f" | wc -l | tr -d ' ')
    else
        old=0
    fi
    if [ -f "$f" ]; then
        new=$(wc -l < "$f" | tr -d ' ')
    else
        new=0
    fi
    delta=$((new - old))
    old_code=$(git_code_lines "$REF" "$f")
    new_code=$(working_code_lines "$f")
    code_delta=$((new_code - old_code))
    printf "%-55s %8d %8d %+8d %8d %8d %+10d\n" "$f" "$old" "$new" "$delta" "$old_code" "$new_code" "$code_delta"
    total_old=$((total_old + old))
    total_new=$((total_new + new))
    code_old_total=$((code_old_total + old_code))
    code_new_total=$((code_new_total + new_code))
done

total_delta=$((total_new - total_old))
code_delta=$((code_new_total - code_old_total))
printf "%-55s %8s %8s %8s %8s %8s %8s\n" "----" "------" "-----" "-----" "--------" "--------" "----------"
printf "%-55s %8d %8d %+8d %8d %8d %+10d\n" "TOTAL (vs $REF)" "$total_old" "$total_new" "$total_delta" "$code_old_total" "$code_new_total" "$code_delta"

if [ "$total_delta" -le "-$MIN_REDUCTION" ] && [ "$code_delta" -le "-$MIN_REDUCTION" ]; then
    echo "PASS: physical LOC reduced by $((-total_delta)); credited code reduced by $((-code_delta)) lines."
    exit 0
else
    echo "FAIL: required reduction $MIN_REDUCTION missed (physical delta $total_delta, code delta $code_delta)." >&2
    exit 1
fi
