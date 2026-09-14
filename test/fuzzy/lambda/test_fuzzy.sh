#!/usr/bin/env bash
# Compatibility entry point for the deterministic Lambda fuzz orchestrator.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
args=()
for arg in "$@"; do
    case "$arg" in
        --duration=*) args+=("--seconds=${arg#*=}") ;;
        --no-lambda-tests) args+=("--no-repo-seeds") ;;
        *) args+=("$arg") ;;
    esac
done

exec python3 "$script_dir/run_fuzz.py" "${args[@]}"
