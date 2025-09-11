#!/bin/bash

# Docker-based KLEE Setup for Lambda Script
# This script sets up KLEE using Docker for easy installation

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

warn() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log "Setting up KLEE using Docker..."

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    error "Docker is not installed. Please install Docker Desktop for macOS."
    error "Download from: https://docs.docker.com/desktop/install/mac-install/"
    exit 1
fi

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    error "Docker is not running. Please start Docker Desktop."
    exit 1
fi

success "Docker is available"

# Create KLEE wrapper scripts
SCRIPT_DIR="$HOME/.local/bin"
mkdir -p "$SCRIPT_DIR"

log "Creating KLEE Docker wrapper scripts..."

# Create klee wrapper
cat > "$SCRIPT_DIR/klee" << 'EOF'
#!/bin/bash
# KLEE Docker wrapper script

# Get the directory of the current working directory
WORK_DIR=$(pwd)

# Run KLEE in Docker container
docker run --rm -v "$WORK_DIR:/work" -w /work klee/klee:latest klee "$@"
EOF

chmod +x "$SCRIPT_DIR/klee"

# Create klee-clang wrapper
cat > "$SCRIPT_DIR/klee-clang" << 'EOF'
#!/bin/bash
# KLEE-Clang Docker wrapper script

# Get the directory of the current working directory
WORK_DIR=$(pwd)

# Run clang in KLEE Docker container
docker run --rm -v "$WORK_DIR:/work" -w /work klee/klee:latest clang "$@"
EOF

chmod +x "$SCRIPT_DIR/klee-clang"

# Create ktest-tool wrapper
cat > "$SCRIPT_DIR/ktest-tool" << 'EOF'
#!/bin/bash
# ktest-tool Docker wrapper script

# Get the directory of the current working directory
WORK_DIR=$(pwd)

# Run ktest-tool in KLEE Docker container
docker run --rm -v "$WORK_DIR:/work" -w /work klee/klee:latest ktest-tool "$@"
EOF

chmod +x "$SCRIPT_DIR/ktest-tool"

# Pull KLEE Docker image
log "Pulling KLEE Docker image (this may take a few minutes)..."
docker pull klee/klee:latest

if [ $? -ne 0 ]; then
    error "Failed to pull KLEE Docker image"
    exit 1
fi

success "KLEE Docker image pulled successfully"

# Add to PATH
SHELL_RC=""
if [ -n "$ZSH_VERSION" ]; then
    SHELL_RC="$HOME/.zshrc"
elif [ -n "$BASH_VERSION" ]; then
    SHELL_RC="$HOME/.bash_profile"
fi

if [ -n "$SHELL_RC" ]; then
    log "Adding KLEE to PATH in $SHELL_RC"
    
    # Remove any existing entries
    grep -v "KLEE Docker" "$SHELL_RC" > "${SHELL_RC}.tmp" 2>/dev/null || touch "${SHELL_RC}.tmp"
    mv "${SHELL_RC}.tmp" "$SHELL_RC"
    
    echo "" >> "$SHELL_RC"
    echo "# KLEE Docker wrapper scripts" >> "$SHELL_RC"
    echo "export PATH=\"$SCRIPT_DIR:\$PATH\"" >> "$SHELL_RC"
fi

# Update current session
export PATH="$SCRIPT_DIR:$PATH"

# Test KLEE installation
log "Testing KLEE installation..."
if "$SCRIPT_DIR/klee" --version >/dev/null 2>&1; then
    success "KLEE Docker setup completed successfully!"
    
    # Show version
    KLEE_VERSION=$("$SCRIPT_DIR/klee" --version 2>&1 | head -1)
    log "KLEE version: $KLEE_VERSION"
    
    # Create a simple test
    TEST_DIR="/tmp/klee_test_$$"
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    log "Creating simple test..."
    cat > test_simple.c << 'EOFTEST'
#include <assert.h>
#include <klee/klee.h>

int main() {
    int x;
    klee_make_symbolic(&x, sizeof(x), "x");
    klee_assume(x >= 0 && x <= 100);
    
    if (x > 50) {
        assert(x <= 100);
    } else {
        assert(x >= 0);
    }
    
    return 0;
}
EOFTEST

    # Test compilation and execution
    log "Testing KLEE compilation..."
    if "$SCRIPT_DIR/klee-clang" -emit-llvm -c -g -O0 test_simple.c -o test_simple.bc 2>/dev/null; then
        success "Test compilation successful!"
        
        log "Testing KLEE execution..."
        if "$SCRIPT_DIR/klee" test_simple.bc >/dev/null 2>&1; then
            TEST_COUNT=$(find . -name "test*.ktest" | wc -l)
            success "KLEE execution successful! Generated $TEST_COUNT test cases."
        else
            warn "KLEE execution had issues, but setup is complete"
        fi
    else
        warn "Test compilation failed, but KLEE is installed"
    fi
    
    # Clean up test
    cd /
    rm -rf "$TEST_DIR"
    
else
    error "KLEE Docker setup failed"
    exit 1
fi

echo
success "🎉 KLEE Docker setup completed!"
echo
echo "📋 Usage:"
echo "  klee --version          # Check KLEE version"
echo "  klee-clang -emit-llvm -c test.c -o test.bc  # Compile to bitcode"
echo "  klee test.bc            # Run symbolic execution"
echo "  ktest-tool test.ktest   # Examine test cases"
echo
echo "🔄 Please restart your terminal or run:"
echo "  source $SHELL_RC"
echo
echo "🚀 Then run Lambda Script KLEE analysis:"
echo "  make klee-all"
echo
echo "💡 Note: This uses Docker containers for KLEE execution."
echo "   All files in your current directory are accessible to KLEE."
