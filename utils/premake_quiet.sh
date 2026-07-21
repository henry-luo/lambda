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
exit "${PIPESTATUS[0]}"
