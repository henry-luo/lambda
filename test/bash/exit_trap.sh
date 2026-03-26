#!/bin/bash
# Tests: 'exit N' builtin with EXIT trap

trap 'echo "cleanup"' EXIT
echo "start"
exit 0
echo "unreachable"
