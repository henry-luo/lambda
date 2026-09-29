#!/bin/bash

# Lambda Release Preparation Script
# This script prepares a Lambda release by copying necessary files and building the release binary.

set -e  # Exit on error

# Get the script's directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Clear the release directory before copying fresh files
rm -rf ./release
mkdir -p ./release

# Runtime assets live in ./lmd/ in the source checkout and ship unchanged as
# ./release/lmd/, so dev and release share one layout. The name "lmd" (not
# "lambda") avoids a clash with the lambda executable on macOS/Linux.

# Step 1: Copy the runtime asset tree (packages, schemas, stylesheets, fonts).
cp -r ./lmd ./release/lmd

# Step 2c: Copy live-demo.html and referenced files
# Copy live-demo.html
mkdir -p ./release/test/html
cp ./test/html/live-demo.html ./release/test/html/

# Copy demo.html as index.html
cp ./test/html/demo.html ./release/test/html/index.html

# Copy HTML files from test/html/ referenced by live-demo.html
for file in flex.html grid.html table.html table_simple.html box.html position.html; do
    if [ -f "./test/html/$file" ]; then
        cp "./test/html/$file" ./release/test/html/
    fi
done

# Copy test/layout/data/page/ files
mkdir -p ./release/test/layout/data/page
if [ -f "./test/layout/data/page/combo_003_complete_article.html" ]; then
    cp "./test/layout/data/page/combo_003_complete_article.html" ./release/test/layout/data/page/
fi

# Copy test/layout/data/res/ files (images/SVGs)
mkdir -p ./release/test/layout/data/res
for file in tiger.svg sample1.png; do
    if [ -f "./test/layout/data/res/$file" ]; then
        cp "./test/layout/data/res/$file" ./release/test/layout/data/res/
    fi
done

# Copy test/input/ files referenced by live-demo.html
mkdir -p ./release/test/input
for file in comprehensive_test.md latex-showcase.tex test.xml test-xml.css raw_commands_test.pdf more_test.yaml; do
    if [ -f "./test/input/$file" ]; then
        cp "./test/input/$file" ./release/test/input/
    fi
done

# Copy test/lambda/ files
mkdir -p ./release/test/lambda
if [ -f "./test/lambda/complex_iot_report_html.ls" ]; then
    cp "./test/lambda/complex_iot_report_html.ls" ./release/test/lambda/
fi

# Copy test/lambda/chart/ files referenced by live-demo.html
mkdir -p ./release/test/lambda/chart
for file in chart_dashboard_demo.ls dashboard_demo.json \
            test_bar_chart.ls bar_chart.json \
            test_line_chart.ls line_chart.json \
            test_area_chart.ls area_chart.json \
            test_scatter_chart.ls scatter_chart.json \
            test_stacked_bar_chart.ls \
            test_grouped_bar_chart.ls \
            test_donut_chart.ls donut_chart.json \
            test_heatmap.ls \
            test_layered_chart.ls layered_chart.json; do
    if [ -f "./test/lambda/chart/$file" ]; then
        cp "./test/lambda/chart/$file" ./release/test/lambda/chart/
    fi
done

# Step 3: Copy doc files (*.md, *.pdf, *.svg, excluding paths starting with '_')
mkdir -p ./release/doc

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
done < <(find ./doc -type f \( -name "*.md" -o -name "*.pdf" -o -name "*.svg" \) -print0)

# Step 4: Require a release binary built by make build-release
if [ ! -x ./lambda.exe ]; then
    echo "Error: lambda.exe not found or not executable."
    echo "Run 'make build-release' before 'make prepare-release', or run 'make release'."
    exit 1
fi

# Step 5: Copy lambda.exe to release directory
# On macOS and Linux rename to 'lambda' (no .exe) to follow POSIX convention and
# avoid a name clash between the executable and the lmd/ asset directory.
if [[ "$OSTYPE" == "darwin"* ]] || [[ "$OSTYPE" == "linux-gnu"* ]]; then
    cp ./lambda.exe ./release/lambda
else
    cp ./lambda.exe ./release/lambda.exe
fi

echo "==> Release preparation complete!"
echo "    Release location: ./release/"
