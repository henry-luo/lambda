#!/bin/bash

# Download script for the 10 new test documents
# 2 INI files, 2 TOML files, 2 RST files, 4 CSS files

BASE_DIR="../../test_output/auto"

echo "📥 Downloading 10 new test documents..."

# INI files
echo "📄 Downloading INI files..."
curl -s -L "https://raw.githubusercontent.com/python/cpython/main/setup.cfg" -o "$BASE_DIR/ini/ini_001_python_setup_cfg.ini"
echo "✅ Downloaded: ini_001_python_setup_cfg.ini"

curl -s -L "https://raw.githubusercontent.com/microsoft/vscode/main/.gitattributes" -o "$BASE_DIR/ini/ini_002_vscode_gitattributes.ini"
echo "✅ Downloaded: ini_002_vscode_gitattributes.ini"

# TOML files
echo "📄 Downloading TOML files..."
curl -s -L "https://raw.githubusercontent.com/psf/black/main/pyproject.toml" -o "$BASE_DIR/toml/toml_001_black_pyproject.toml"
echo "✅ Downloaded: toml_001_black_pyproject.toml"

curl -s -L "https://raw.githubusercontent.com/rust-lang/cargo/master/Cargo.toml" -o "$BASE_DIR/toml/toml_002_cargo_manifest.toml"
echo "✅ Downloaded: toml_002_cargo_manifest.toml"

# RST files  
echo "📄 Downloading RST files..."
curl -s -L "https://raw.githubusercontent.com/python/peps/main/pep-0001.rst" -o "$BASE_DIR/rst/rst_001_pep_0001.rst"
echo "✅ Downloaded: rst_001_pep_0001.rst"

curl -s -L "https://raw.githubusercontent.com/sphinx-doc/sphinx/master/README.rst" -o "$BASE_DIR/rst/rst_002_sphinx_readme.rst"
echo "✅ Downloaded: rst_002_sphinx_readme.rst"

# CSS files
echo "📄 Downloading CSS files..."
curl -s -L "https://raw.githubusercontent.com/twbs/bootstrap/main/dist/css/bootstrap.css" -o "$BASE_DIR/css/css_001_bootstrap_framework.css"
echo "✅ Downloaded: css_001_bootstrap_framework.css"

curl -s -L "https://raw.githubusercontent.com/necolas/normalize.css/master/normalize.css" -o "$BASE_DIR/css/css_002_normalize_reset.css"
echo "✅ Downloaded: css_002_normalize_reset.css"

curl -s -L "https://raw.githubusercontent.com/animate-css/animate.css/main/animate.css" -o "$BASE_DIR/css/css_003_animate_library.css"
echo "✅ Downloaded: css_003_animate_library.css"

curl -s -L "https://raw.githubusercontent.com/tailwindlabs/tailwindcss/master/src/css/base.css" -o "$BASE_DIR/css/css_004_tailwind_base.css"
echo "✅ Downloaded: css_004_tailwind_base.css"

echo "🎉 Successfully downloaded all 10 documents!"
echo ""
echo "📊 Summary:"
echo "   INI files:  2"
echo "   TOML files: 2" 
echo "   RST files:  2"
echo "   CSS files:  4"
echo "   Total:      10"
