#!/usr/bin/env bash
# Wrapper around premake5 that drops its per-file "Generated <file>..." progress
# lines (185+ on a clean `gmake`) while preserving the banner/Done lines, any
# error output, and — crucially — premake5's own exit status.
#
# Usage: premake_quiet.sh <premake5-binary> [premake5 args...]
#
# PIPESTATUS[0] carries premake5's real exit code; the `|| true` keeps a fully
# filtered stream (which would make grep exit 1) from masquerading as failure.
pm=$1
shift
"$pm" "$@" | { grep -vE '^Generated ' || true; }
premake_status=${PIPESTATUS[0]}
if [ "$premake_status" -eq 0 ]; then
    for argument in "$@"; do
        case "${argument##*/}" in
            premake5.mac.lua|premake5.lin.lua|premake5.win.lua|--file=premake5.mac.lua|--file=premake5.lin.lua|--file=premake5.win.lua)
                tool_directory=$(cd "$(dirname "$0")" && pwd)
                PYTHONDONTWRITEBYTECODE=1 python3 "$tool_directory/check_radiant_archive_ownership.py" --prepare || exit $?
                break
                ;;
        esac
    done
fi
exit "$premake_status"
