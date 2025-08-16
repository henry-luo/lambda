# Lambda Script Cheatsheet - LaTeX Templates Guide

## PDF Generation

### Quick Start
Use the provided shell script to generate both PDFs:
```bash
./generate_pdf_cheatsheets.sh
```

### Manual Generation Commands

#### Landscape Version
```bash
pandoc Lambda_Cheatsheet.md -o Lambda_Cheatsheet.pdf \
  --template=template_landscape.tex \
  --pdf-engine=xelatex \
  -V geometry:a4paper,landscape,margin=0.5in
```

#### Portrait Version  
```bash
pandoc Lambda_Cheatsheet.md -o Lambda_Cheatsheet_Portrait.pdf \
  --template=template_portrait.tex \
  --pdf-engine=xelatex \
  -V geometry:a4paper,portrait,margin=0.5in
```

## Template Files

### 1. Landscape Template
- **File:** `template_landscape.tex`
- **Output:** `Lambda_Cheatsheet.pdf`
- **Layout:** Landscape A4, 3 columns
- **Margins:** 0.3in (template) / 0.5in (command override)

### 2. Portrait Template  
- **File:** `template_portrait.tex`
- **Output:** `Lambda_Cheatsheet_Portrait.pdf`
- **Layout:** Portrait A4, 3 columns
- **Margins:** 0.4in (template) / 0.5in (command override)

## Code Block Formatting Customization

### Current Configuration
Both templates use the same `Shaded` environment for code blocks:

```latex
\newenvironment{Shaded}{%
  \begin{snugshade}%
  \vspace{0.4\baselineskip}%      % Top padding
  \fontsize{10.5}{11.5}\selectfont% % Font size and line spacing
  \setlength{\parskip}{0pt}%
  \setlength{\topsep}{0pt}%
  \setlength{\partopsep}{0pt}%
}{%
  \vspace{0.01\baselineskip}%     % Bottom padding
  \end{snugshade}%
}
```

### Tuning Parameters

#### 1. Font Size and Line Spacing
```latex
\fontsize{FONT_SIZE}{LINE_SPACING}\selectfont%
```
- **Current:** `\fontsize{10.5}{11.5}` (10.5pt font, 11.5pt line spacing)
- **Evolution:** 8pt → 8.5pt → 9pt → 9.5pt → 10.5pt
- **Recommendations:**
  - Increase font size: Use larger first number (e.g., `11`, `11.5`)
  - Increase line spacing: Use larger second number (e.g., `12`, `12.5`)
  - Keep ratio: Line spacing should be ~1.1x font size for readability

#### 2. Padding Control
```latex
\vspace{TOP_PADDING\baselineskip}%      % Top padding
% ... code content ...
\vspace{BOTTOM_PADDING\baselineskip}%   % Bottom padding
```
- **Current:** Top: `0.4`, Bottom: `0.01`
- **History:** Various combinations tested for visual balance
- **Guidelines:**
  - `0.0` = No padding
  - `0.1` = Very minimal padding
  - `0.2-0.3` = Light padding
  - `0.4-0.5` = Standard padding
  - `0.6+` = Heavy padding
- **Note:** Bottom padding often appears larger due to LaTeX's inherent spacing

#### 3. Background Color
```latex
\definecolor{shadecolor}{rgb}{0.97,0.97,0.97}
```
- **Current:** Light gray (97% white)
- **Alternatives:**
  - `{0.95,0.95,0.95}` = Slightly darker gray
  - `{0.98,0.98,0.98}` = Lighter gray
  - `{0.95,0.98,1.0}` = Light blue tint

### Location in Templates
The `Shaded` environment definition appears around **lines 35-45** in both template files, immediately after the color definitions and before the title formatting.

### Testing Changes
After modifying the `Shaded` environment:
1. Save the template file(s)
2. Run `./generate_pdf_cheatsheets.sh` or the manual pandoc commands
3. Check the generated PDF(s) for visual appearance
4. Iterate as needed

### Common Adjustments

#### Make Code Blocks More Compact
```latex
\vspace{0.2\baselineskip}%      % Reduced top padding
\fontsize{10}{11}\selectfont%   % Smaller font
% ...
\vspace{0.0\baselineskip}%      % No bottom padding
```

#### Make Code Blocks More Readable
```latex
\vspace{0.5\baselineskip}%      % More top padding
\fontsize{11}{12.5}\selectfont% % Larger font with generous line spacing
% ...
\vspace{0.1\baselineskip}%      % Minimal bottom padding
```

#### Balance Visual Appearance
The current settings (0.4 top, 0.01 bottom, 10.5pt font) were chosen through iterative testing to provide:
- Good readability without being too large
- Visually balanced padding despite LaTeX's quirks
- Proper fit within the 3-column layout

## Additional Features
- ✅ Centered title with right-aligned version info
- ✅ Page numbers in footer
- ✅ Orphan/widow control for better page breaks
- ✅ `multicols*` environment prevents unwanted page breaks
- ✅ Optimized column separation and text flow
- ✅ Complete pandoc syntax highlighting support
