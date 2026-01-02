#!/bin/bash

# Lambda Release Preparation Script
# This script prepares a Lambda release by copying necessary files and building the release binary.

set -e  # Exit on error

# Get the script's directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "==> Preparing Lambda release..."

# Step 1: Create release/lambda/input directory recursively
echo "==> Creating release/lambda/input directory..."
mkdir -p ./release/lambda/input

# Step 2: Copy Lambda input files (*.ls and *.css)
echo "==> Copying Lambda input files..."
if ls ./lambda/input/*.ls >/dev/null 2>&1; then
    cp ./lambda/input/*.ls ./release/lambda/input/
    echo "    Copied *.ls files"
fi

if ls ./lambda/input/*.css >/dev/null 2>&1; then
    cp ./lambda/input/*.css ./release/lambda/input/
    echo "    Copied *.css files"
fi

# Step 3: Copy doc files (excluding files starting with '_')
echo "==> Creating release/doc directory..."
mkdir -p ./release/doc

echo "==> Copying documentation files..."
for file in ./doc/*; do
    filename=$(basename "$file")
    # Skip files starting with '_'
    if [[ ! "$filename" =~ ^_ ]]; then
        cp "$file" ./release/doc/
        echo "    Copied $filename"
    fi
done

# Step 4: Build release binary
echo "==> Building release binary..."
make build-release

# Step 5: Copy lambda.exe to release directory
echo "==> Copying lambda.exe to release directory..."
cp ./lambda.exe ./release/

echo "==> Release preparation complete!"
echo "    Release location: ./release/"
