#!/bin/bash
# Lambda Script Automated Testing Runner
# Wrapper script for the Python automation system

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "=== Lambda Script Automated Testing System ==="
echo "Project Root: $PROJECT_ROOT"
echo "Automation Script: $SCRIPT_DIR/auto_test_runner.py"
echo

# Check if Lambda executable exists
LAMBDA_EXE="$PROJECT_ROOT/lambda.exe"
if [ ! -f "$LAMBDA_EXE" ]; then
    echo "Error: Lambda executable not found at $LAMBDA_EXE"
    echo "Please run 'make build' first to compile Lambda"
    exit 1
fi

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 is required but not installed"
    exit 1
fi

# Activate virtual environment if it exists
VENV_DIR="$SCRIPT_DIR/../venv"
if [ -d "$VENV_DIR" ]; then
    echo "Activating virtual environment..."
    source "$VENV_DIR/bin/activate"
else
    echo "Warning: Virtual environment not found at $VENV_DIR"
    echo "Creating virtual environment..."
    python3 -m venv "$VENV_DIR"
    source "$VENV_DIR/bin/activate"
    echo "Installing required packages..."
    pip install requests
fi

# Check for required Python packages
echo "Checking Python dependencies..."
python -c "import requests" 2>/dev/null || {
    echo "Error: Python 'requests' package is required"
    echo "Installing requests..."
    pip install requests
}

# Run the automation script
echo "Starting automated testing..."
echo
python "$SCRIPT_DIR/auto_test_runner.py" "$@"
