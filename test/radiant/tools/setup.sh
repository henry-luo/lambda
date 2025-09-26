#!/bin/bash

# Setup script for the Radiant Layout Extractor
# This script installs dependencies and sets up the testing environment

set -e

echo "🚀 Setting up Radiant Layout Extractor..."

# Check if Node.js is installed
if ! command -v node &> /dev/null; then
    echo "❌ Node.js is not installed. Please install Node.js 16+ first."
    echo "Visit: https://nodejs.org/"
    exit 1
fi

# Check Node.js version
NODE_VERSION=$(node --version | cut -d'v' -f2 | cut -d'.' -f1)
if [ "$NODE_VERSION" -lt "16" ]; then
    echo "❌ Node.js version 16+ is required. Current version: $(node --version)"
    exit 1
fi

echo "✓ Node.js $(node --version) detected"

# Install dependencies
echo "📦 Installing npm dependencies..."
npm install

echo "🧪 Running test extraction to verify setup..."
node test_extractor.js

echo ""
echo "✅ Setup complete! The Layout Extractor is ready to use."
echo ""
echo "Usage examples:"
echo "  # Extract from HTML/CSS files:"
echo "  node layout_extractor.js extract-single test.html test.css output.json"
echo ""
echo "  # Batch process a directory:"
echo "  node layout_extractor.js extract-batch ./input_dir ./output_dir"
echo ""
echo "  # Extract from inline HTML/CSS:"
echo "  node layout_extractor.js extract-inline '<div>test</div>' 'div { color: red; }'"
echo ""
echo "Sample test files generated:"
echo "  - sample_flexbox_test.json"
echo "  - sample_block_test.json"
echo "  - sample_nested_test.json"
