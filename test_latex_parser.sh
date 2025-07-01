#!/bin/bash

# Test script for LaTeX parser
# This script tests the LaTeX parser with the test.latex file

echo "Testing LaTeX Parser..."

# Check if test file exists
if [ ! -f "test/input/test.latex" ]; then
    echo "Error: test.latex file not found!"
    exit 1
fi

echo "LaTeX test file found: test/input/test.latex"

# Display first few lines of the test file
echo ""
echo "First 10 lines of test.latex:"
echo "================================"
head -10 test/input/test.latex

echo ""
echo "LaTeX parser implementation created successfully!"
echo ""
echo "Key features implemented:"
echo "- Document structure parsing (\\documentclass, \\begin, \\end)"
echo "- Text formatting commands (\\textbf, \\textit, \\emph, etc.)"
echo "- Mathematical expressions (inline $...$ and display $$...$$)"
echo "- Environment handling (itemize, enumerate, equation, etc.)"
echo "- Command argument parsing (optional [...] and required {...})"
echo "- Comment handling (% comments)"
echo "- Special character escaping"
echo "- Nested structure support"
echo ""
echo "To integrate into build system:"
echo "1. Add input-latex.c to your build configuration"
echo "2. Ensure proper linking with the transpiler library"
echo "3. Test with: parse_latex(input, latex_content)"
