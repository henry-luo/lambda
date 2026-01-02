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

# Step 2b: Copy LaTeX CSS files
echo "==> Creating release/lambda/input/latex/css directory..."
mkdir -p ./release/lambda/input/latex/css

echo "==> Copying LaTeX CSS files..."
if ls ./lambda/input/latex/css/*.css >/dev/null 2>&1; then
    cp ./lambda/input/latex/css/*.css ./release/lambda/input/latex/css/
    echo "    Copied LaTeX CSS files"
fi

# Step 2b2: Copy LaTeX fonts (Computer Modern)
echo "==> Copying LaTeX fonts..."
mkdir -p ./release/lambda/input/latex/fonts

# Copy main font CSS files
if [ -f "./lambda/input/latex/fonts/cmu.css" ]; then
    cp ./lambda/input/latex/fonts/cmu.css ./release/lambda/input/latex/fonts/
    echo "    Copied fonts/cmu.css"
fi

if [ -f "./lambda/input/latex/fonts/cmu-combined.css" ]; then
    cp ./lambda/input/latex/fonts/cmu-combined.css ./release/lambda/input/latex/fonts/
    echo "    Copied fonts/cmu-combined.css"
fi

# Copy font directories (Serif, Sans, Typewriter, etc.)
for fontdir in "Serif" "Serif Slanted" "Sans" "Typewriter" "Typewriter Slanted"; do
    if [ -d "./lambda/input/latex/fonts/$fontdir" ]; then
        mkdir -p "./release/lambda/input/latex/fonts/$fontdir"
        cp "./lambda/input/latex/fonts/$fontdir/"* "./release/lambda/input/latex/fonts/$fontdir/" 2>/dev/null
        echo "    Copied fonts/$fontdir"
    fi
done

# Step 2c: Copy live-demo.html and referenced files
echo "==> Copying live-demo.html and referenced files..."

# Copy live-demo.html
mkdir -p ./release/test/html
cp ./test/html/live-demo.html ./release/test/html/
echo "    Copied test/html/live-demo.html"

# Copy demo.html as index.html
cp ./test/html/demo.html ./release/test/html/index.html
echo "    Copied test/html/demo.html as index.html"

# Copy HTML files from test/html/ referenced by live-demo.html
for file in flex.html grid.html table.html table_simple.html box.html position.html; do
    if [ -f "./test/html/$file" ]; then
        cp "./test/html/$file" ./release/test/html/
        echo "    Copied test/html/$file"
    fi
done

# Copy test/layout/data/page/ files
mkdir -p ./release/test/layout/data/page
if [ -f "./test/layout/data/page/combo_003_complete_article.html" ]; then
    cp "./test/layout/data/page/combo_003_complete_article.html" ./release/test/layout/data/page/
    echo "    Copied test/layout/data/page/combo_003_complete_article.html"
fi

# Copy test/layout/data/res/ files (images/SVGs)
mkdir -p ./release/test/layout/data/res
for file in tiger.svg sample1.png; do
    if [ -f "./test/layout/data/res/$file" ]; then
        cp "./test/layout/data/res/$file" ./release/test/layout/data/res/
        echo "    Copied test/layout/data/res/$file"
    fi
done

# Copy test/input/ files referenced by live-demo.html
mkdir -p ./release/test/input
for file in comprehensive_test.md latex-showcase.tex test.xml raw_commands_test.pdf more_test.yaml; do
    if [ -f "./test/input/$file" ]; then
        cp "./test/input/$file" ./release/test/input/
        echo "    Copied test/input/$file"
    fi
done

# Copy test/lambda/ files
mkdir -p ./release/test/lambda
if [ -f "./test/lambda/complex_iot_report_html.ls" ]; then
    cp "./test/lambda/complex_iot_report_html.ls" ./release/test/lambda/
    echo "    Copied test/lambda/complex_iot_report_html.ls"
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
