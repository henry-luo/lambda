#!/bin/bash
# Lambda Development Container Entrypoint
set -e

# If the first argument is "sshd", start SSH daemon
if [ "$1" = "sshd" ] || [ "$1" = "/usr/sbin/sshd" ]; then
    echo "Starting SSH daemon for VS Code Remote SSH..."
    # Start sshd as root (requires sudo since we run as lambda user)
    exec sudo /usr/sbin/sshd -D -e
fi

# If workspace has setup script and deps haven't been built yet, offer guidance
if [ -f /workspace/setup-linux-deps.sh ] && [ ! -f /workspace/lambda/tree-sitter/libtree-sitter.a ]; then
    echo "=========================================="
    echo " Lambda Development Environment"
    echo "=========================================="
    echo ""
    echo " First-time setup:"
    echo "   ./setup-linux-deps.sh    # Install & build native deps"
    echo "   make build               # Build Lambda (debug)"
    echo "   make test                # Run tests"
    echo ""
    echo " Quick reference:"
    echo "   make release             # Release build"
    echo "   make build-test          # Build test executables"
    echo "   make generate-grammar    # Regenerate parser"
    echo "=========================================="
    echo ""
fi

exec "$@"
