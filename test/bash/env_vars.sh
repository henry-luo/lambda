#!/bin/bash
# Test: environment variable integration (Phase 7.2)

# Check that HOME is imported from environment
if [ -n "$HOME" ]; then
    echo "HOME is set"
else
    echo "HOME is empty"
fi

# Check that USER is imported from environment
if [ -n "$USER" ]; then
    echo "USER is set"
else
    echo "USER is empty"
fi

# Check that PATH is imported from environment
if [ -n "$PATH" ]; then
    echo "PATH is set"
else
    echo "PATH is empty"
fi

# Test export: set and export a variable
MY_EXPORTED="hello"
export MY_EXPORTED
echo "exported: $MY_EXPORTED"

# Test that exported var is accessible after re-reading
echo "re-read: $MY_EXPORTED"

# Test export with value in one statement
export ANOTHER_VAR="world"
echo "ANOTHER_VAR: $ANOTHER_VAR"
