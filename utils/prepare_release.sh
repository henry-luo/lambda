#!/bin/bash

# Lambda Release Preparation Script
# This script prepares a Lambda release by copying necessary files and building the release binary.

set -e  # Exit on error

# Get the script's directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "==> Preparing Lambda release..."

# Runtime assets are copied to ./release/lmd/ (not ./release/lambda/) to avoid a name clash
# between the lambda executable and a directory of the same name on macOS/Linux.

# Step 1: Create release/lmd/input directory recursively
echo "==> Creating release/lmd/input directory..."
mkdir -p ./release/lmd/input

# Step 2: Copy Lambda input files (*.ls and *.css)
echo "==> Copying Lambda input files..."
if ls ./lambda/input/*.ls >/dev/null 2>&1; then
    cp ./lambda/input/*.ls ./release/lmd/input/
    echo "    Copied *.ls files"
fi

if ls ./lambda/input/*.css >/dev/null 2>&1; then
    cp ./lambda/input/*.css ./release/lmd/input/
    echo "    Copied *.css files"
fi

# Step 2b: Copy LaTeX CSS files
echo "==> Creating release/lmd/input/latex/css directory..."
mkdir -p ./release/lmd/input/latex/css

echo "==> Copying LaTeX CSS files..."
if ls ./lambda/input/latex/css/*.css >/dev/null 2>&1; then
    cp ./lambda/input/latex/css/*.css ./release/lmd/input/latex/css/
    echo "    Copied LaTeX CSS files"
fi

# Step 2b2: Copy LaTeX fonts (Computer Modern + KaTeX)
# Fonts used by latex package: Computer Modern Serif, Typewriter, Sans (CMU woff files in subdirs)
# Fonts used by math package: KaTeX_* woff2 files (KaTeX_AMS, Caligraphic, Fraktur, Main, Math,
#   SansSerif, Script, Size1-4, Typewriter)
# Copy the entire fonts directory to ensure all referenced assets are present.
echo "==> Copying LaTeX fonts (Computer Modern + KaTeX)..."
rm -rf ./release/lmd/input/latex/fonts
cp -r ./lambda/input/latex/fonts ./release/lmd/input/latex/fonts
echo "    Copied lambda/input/latex/fonts/ (KaTeX woff2 + CMU woff subdirectories)"

# Step 2d: Copy Lambda packages
echo "==> Copying Lambda packages..."
rm -rf ./release/lmd/package
cp -r ./lambda/package ./release/lmd/package
echo "    Copied lambda/package/ (chart, latex, math)"

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
for file in comprehensive_test.md latex-showcase.tex test.xml test-xml.css raw_commands_test.pdf more_test.yaml; do
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

# Step 3: Copy doc files (*.md, *.pdf, *.svg, excluding paths starting with '_')
echo "==> Creating release/doc directory..."
mkdir -p ./release/doc

echo "==> Copying documentation files (*.md, *.pdf, *.svg)..."
while IFS= read -r -d '' file; do
    # Get path relative to ./doc
    relpath="${file#./doc/}"
    # Skip any path component starting with '_'
    if echo "$relpath" | grep -qE '(^|/)_'; then
        continue
    fi
    destdir="./release/doc/$(dirname "$relpath")"
    mkdir -p "$destdir"
    cp "$file" "$destdir/"
    echo "    Copied doc/$relpath"
done < <(find ./doc -type f \( -name "*.md" -o -name "*.pdf" -o -name "*.svg" \) -print0)

# Step 4: Build release binary
echo "==> Building release binary..."
make build-release

# Step 5: Copy lambda.exe to release directory
# On macOS and Linux rename to 'lambda' (no .exe) to follow POSIX convention and
# avoid a name clash between the executable and the lmd/ asset directory.
echo "==> Copying lambda.exe to release directory..."
if [[ "$OSTYPE" == "darwin"* ]] || [[ "$OSTYPE" == "linux-gnu"* ]]; then
    cp ./lambda.exe ./release/lambda
    echo "    Copied lambda.exe as release/lambda"
else
    cp ./lambda.exe ./release/lambda.exe
    echo "    Copied lambda.exe to release/"
fi

echo "==> Release preparation complete!"
echo "    Release location: ./release/"
