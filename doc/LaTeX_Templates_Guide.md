# Lambda Script Cheatsheet - LaTeX Templates

## Final Templates (Production)

### 1. Landscape Version Template
- **File:** `simple_multicolumn_template.tex`
- **Generates:** `Lambda_Cheatsheet.pdf`
- **Layout:** Landscape A4, 3 columns
- **Command:** `pandoc Lambda_Cheatsheet.md -o Lambda_Cheatsheet.pdf --template=simple_multicolumn_template.tex --pdf-engine=pdflatex -V geometry:margin=0.5in`

### 2. Portrait Version Template  
- **File:** `portrait_multicolumn_template.tex`
- **Generates:** `Lambda_Cheatsheet_Portrait.pdf`
- **Layout:** Portrait A4, 3 columns
- **Command:** `pandoc Lambda_Cheatsheet.md -o Lambda_Cheatsheet_Portrait.pdf --template=portrait_multicolumn_template.tex --pdf-engine=pdflatex -V geometry:margin=0.4in`

## Template Features (Both)
- ✅ Multi-column layout with clean separation
- ✅ Preserved line breaks in code blocks using `fancyvrb`
- ✅ No black borders on code blocks
- ✅ `\scriptsize` font for code blocks
- ✅ Half-line internal padding within gray code block backgrounds
- ✅ Optimized section spacing and typography
- ✅ Complete pandoc syntax highlighting support

## Removed Templates
- `multicolumn_template.tex` - Earlier experimental version
- `listings_template.tex` - Alternative approach that didn't work as well

The remaining two templates represent the final, optimized versions for both orientations.
