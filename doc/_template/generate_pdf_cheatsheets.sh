#!/bin/bash

# Lambda Script PDF Cheatsheet Generator
# This script generates both landscape and portrait versions of the Lambda Script cheatsheet

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BLUE}Lambda Script PDF Cheatsheet Generator${NC}"
echo "=================================="

# Check if required files exist
if [[ ! -f "../Lambda_Cheatsheet.md" ]]; then
    echo -e "${RED}Error: Lambda_Cheatsheet.md not found in parent directory${NC}"
    exit 1
fi

if [[ ! -f "template_landscape.tex" ]]; then
    echo -e "${RED}Error: template_landscape.tex not found${NC}"
    exit 1
fi

if [[ ! -f "template_portrait.tex" ]]; then
    echo -e "${RED}Error: template_portrait.tex not found${NC}"
    exit 1
fi

# Check if pandoc is installed
if ! command -v pandoc &> /dev/null; then
    echo -e "${RED}Error: pandoc is not installed. Please install pandoc first.${NC}"
    echo "On macOS: brew install pandoc"
    echo "On Linux: sudo apt-get install pandoc (Ubuntu/Debian) or sudo yum install pandoc (RHEL/CentOS)"
    exit 1
fi

# Check if xelatex is available
if ! command -v xelatex &> /dev/null; then
    echo -e "${RED}Error: xelatex is not installed. Please install a LaTeX distribution.${NC}"
    echo "On macOS: brew install --cask mactex"
    echo "On Linux: sudo apt-get install texlive-xetex (Ubuntu/Debian)"
    exit 1
fi

# Check and install required LaTeX packages
check_and_install_latex_package() {
    local pkg=$1
    if ! kpsewhich "${pkg}.sty" &> /dev/null; then
        echo -e "${YELLOW}Installing missing LaTeX package: ${pkg}${NC}"
        if command -v tlmgr &> /dev/null; then
            sudo tlmgr install "$pkg" || {
                echo -e "${RED}Failed to install ${pkg}. Please install manually: sudo tlmgr install ${pkg}${NC}"
                exit 1
            }
        else
            echo -e "${RED}Error: tlmgr not found. Please install ${pkg} manually.${NC}"
            exit 1
        fi
    fi
}

echo -e "${BLUE}Checking LaTeX packages...${NC}"
check_and_install_latex_package "titlesec"
check_and_install_latex_package "framed"

echo -e "${YELLOW}Generating PDF cheatsheets...${NC}"
echo

# Generate landscape PDF
echo -e "${BLUE}Generating landscape PDF...${NC}"
if pandoc ../Lambda_Cheatsheet.md \
    -o ../Lambda_Cheatsheet.pdf \
    --template=template_landscape.tex \
    --pdf-engine=xelatex \
    -V geometry:a4paper,landscape,margin=0.5in; then

    if [[ -f "../Lambda_Cheatsheet.pdf" ]]; then
        PDF_SIZE=$(stat -f%z "../Lambda_Cheatsheet.pdf" 2>/dev/null || stat -c%s "../Lambda_Cheatsheet.pdf" 2>/dev/null || echo "unknown")
        echo -e "${GREEN}✓ Landscape PDF generated successfully${NC}"
        echo "  File: ../Lambda_Cheatsheet.pdf (${PDF_SIZE} bytes)"
    else
        echo -e "${RED}✗ Landscape PDF generation failed${NC}"
        exit 1
    fi
else
    echo -e "${RED}✗ Landscape PDF generation failed${NC}"
    exit 1
fi

echo

# Generate portrait PDF
echo -e "${BLUE}Generating portrait PDF...${NC}"
if pandoc ../Lambda_Cheatsheet.md \
    -o Lambda_Cheatsheet_Portrait.pdf \
    --template=template_portrait.tex \
    --pdf-engine=xelatex \
    -V geometry:a4paper,portrait,margin=0.5in; then

    if [[ -f "Lambda_Cheatsheet_Portrait.pdf" ]]; then
        PDF_SIZE=$(stat -f%z "Lambda_Cheatsheet_Portrait.pdf" 2>/dev/null || stat -c%s "Lambda_Cheatsheet_Portrait.pdf" 2>/dev/null || echo "unknown")
        echo -e "${GREEN}✓ Portrait PDF generated successfully${NC}"
        echo "  File: Lambda_Cheatsheet_Portrait.pdf (${PDF_SIZE} bytes)"
    else
        echo -e "${RED}✗ Portrait PDF generation failed${NC}"
        exit 1
    fi
else
    echo -e "${RED}✗ Portrait PDF generation failed${NC}"
    exit 1
fi

echo
echo -e "${GREEN}All PDFs generated successfully!${NC}"
echo
echo "Generated files:"
echo "  - ../Lambda_Cheatsheet.pdf (landscape, 3-column)"
echo "  - Lambda_Cheatsheet_Portrait.pdf (portrait, 3-column)"
echo
echo -e "${YELLOW}Tips:${NC}"
echo "  - To customize formatting, edit the template_*.tex files"
echo "  - See LaTeX_Templates_Guide.md for detailed customization options"
echo "  - For code block formatting, see the 'Shaded' environment in templates"
echo
