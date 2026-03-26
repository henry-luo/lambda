#!/bin/bash
# Phase 8: trap and eval tests

# EXIT trap runs at script end (normal exit)
trap 'echo "exit-trap"' EXIT
echo "before"

# change the trap handler
trap 'echo "new-handler"' EXIT
echo "after-retrap"

# trap reset removes handler
trap '-' EXIT
echo "trap-cleared"

# re-register EXIT trap (will run at end)
trap 'echo "final-cleanup"' EXIT

# ignore TERM signal
trap '' TERM
echo "term-ignored"

echo "done"
