#!/bin/bash
# Test: set builtin options (Phase 7.3)

# set -x tracing goes to stderr - we test it doesn't crash and stdout is clean
set -x
x=42
set +x
echo "x=$x"

# set -e: exit on error (test that script continues when commands succeed)
set -e
echo "after set -e"
true
echo "true succeeded"
set +e

# set using -o pipefail style
set -o pipefail
echo "pipefail enabled"
set +o pipefail
echo "pipefail disabled"

# multiple flags at once
set -eu
echo "eu flags set"
set +eu
echo "eu flags cleared"
