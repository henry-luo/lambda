#!/bin/bash

# LaTeX Math Test Setup Script
# Installs dependencies and generates baseline reference DVIs

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "LaTeX Math Test Framework Setup"
echo "=========================================="
echo ""

# Check Node.js
if ! command -v node &> /dev/null; then
    echo "❌ Node.js not found. Please install Node.js v16 or later."
    exit 1
fi

NODE_VERSION=$(node -v | cut -d'.' -f1 | sed 's/v//')
if [ "$NODE_VERSION" -lt 16 ]; then
    echo "⚠️  Node.js version $NODE_VERSION detected. Recommend v16 or later."
fi

echo "✅ Node.js found: $(node -v)"
echo ""

# Install npm dependencies
echo "Installing npm dependencies..."
npm install

if [ $? -ne 0 ]; then
    echo "❌ npm install failed"
    exit 1
fi

echo "✅ Dependencies installed"
echo ""

# Check for pdflatex (optional)
if command -v pdflatex &> /dev/null; then
    echo "✅ pdflatex found: $(pdflatex --version | head -n1)"
    echo ""

    # Generate baseline reference DVIs
    echo "Generating baseline reference DVI files..."
    mkdir -p reference

    for texfile in baseline/*.tex; do
        if [ -f "$texfile" ]; then
            basename=$(basename "$texfile" .tex)
            echo "  Compiling: $basename"
            pdflatex -output-directory=reference -interaction=nonstopmode "$texfile" > /dev/null 2>&1 || {
                echo "    ⚠️  Warning: $basename failed to compile"
            }
        fi
    done

    echo "✅ Baseline reference DVIs generated"
    echo ""
else
    echo "⚠️  pdflatex not found. Baseline DVI references will not be generated."
    echo "   Install TeX Live or MiKTeX to enable baseline testing."
    echo ""
fi

# Check for Lambda executable
LAMBDA_EXE="../../lambda.exe"
if [ -f "$LAMBDA_EXE" ]; then
    echo "✅ Lambda executable found"
else
    echo "⚠️  Lambda executable not found at $LAMBDA_EXE"
    echo "   Run 'make build' in project root to compile Lambda."
fi

echo ""
echo "=========================================="
echo "Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. Run tests:            npm test"
echo "  2. Generate references:  npm run generate:mathlive"
echo "  3. Verbose output:       npm run test:verbose"
echo ""
echo "Or use Makefile targets:"
echo "  make test-math"
echo "  make test-math-baseline"
echo "  make test-math-extended"
echo ""
