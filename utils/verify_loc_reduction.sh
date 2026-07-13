#!/usr/bin/env bash
# Verify that a clean-up phase actually reduced lines of code.
#
# Compares the total LOC of a file set between a git ref (default: HEAD)
# and the working tree. New helper files created by the phase MUST be
# listed too, so the total is honest (a phase that moves 300 lines into
# a new header saves nothing).
#
# Usage:
#   ./utils/verify_loc_reduction.sh [--ref <git-ref>] <file> [file...]
#   ./utils/verify_loc_reduction.sh --ref master radiant/resolve_css_style.cpp radiant/resolve_style_helpers.hpp
#
# Exit code: 0 if total LOC strictly decreased, 1 otherwise.

set -euo pipefail

REF="HEAD"
if [ "${1:-}" = "--ref" ]; then
    REF="$2"
    shift 2
fi

if [ $# -eq 0 ]; then
    echo "usage: $0 [--ref <git-ref>] <file> [file...]" >&2
    exit 2
fi

total_old=0
total_new=0

printf "%-55s %8s %8s %8s\n" "file" "before" "after" "delta"
printf "%-55s %8s %8s %8s\n" "----" "------" "-----" "-----"

for f in "$@"; do
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
    printf "%-55s %8d %8d %+8d\n" "$f" "$old" "$new" "$delta"
    total_old=$((total_old + old))
    total_new=$((total_new + new))
done

total_delta=$((total_new - total_old))
printf "%-55s %8s %8s %8s\n" "----" "------" "-----" "-----"
printf "%-55s %8d %8d %+8d\n" "TOTAL (vs $REF)" "$total_old" "$total_new" "$total_delta"

if [ "$total_delta" -lt 0 ]; then
    echo "PASS: total LOC reduced by $((-total_delta)) lines."
    exit 0
else
    echo "FAIL: total LOC did not decrease (delta $total_delta)." >&2
    exit 1
fi
