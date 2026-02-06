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
  \begin{shaded}%                     % Breakable background
  \vspace{0.1\baselineskip}%          % Reduced top padding
  \fontsize{10.5}{11.5}\selectfont%   % Font size and line spacing
  \setlength{\parskip}{0pt}%
  \setlength{\topsep}{0pt}%
  \setlength{\partopsep}{0pt}%
  \setlength{\leftskip}{1em}%         % Left margin (like prose)
  \setlength{\rightskip}{1em}%        % Right margin (like prose)
}{%
  \vspace{0pt}%                       % No bottom padding
  \end{shaded}%
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

#### 2. Padding and Margin Control
```latex
\vspace{TOP_PADDING\baselineskip}%      % Top padding
\setlength{\leftskip}{LEFT_MARGIN}%     % Left margin
\setlength{\rightskip}{RIGHT_MARGIN}%   % Right margin
% ... code content ...
\vspace{BOTTOM_PADDING\baselineskip}%   % Bottom padding
```
- **Current:** Top: `0.1`, Bottom: `0pt` (reduced by 0.3 lines from original)
- **Current margins:** Left: `1em`, Right: `1em` (aligns with prose text)
- **History:** Top was 0.4→0.1, Bottom was 0.01→0pt
- **Guidelines:**
  - `0.0` = No padding
  - `0.1` = Very minimal padding
  - `0.2-0.3` = Light padding
  - `0.4-0.5` = Standard padding
  - `0.6+` = Heavy padding
- **Margin guidelines:**
  - `1em` = Standard text indentation
  - `0.5em` = Light indentation
  - `1.5em` = Heavy indentation
  - `0pt` = No margins (full width)
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

#### 4. Page Breaking Behavior
```latex
% In Highlighting environment:
\DefineVerbatimEnvironment{Highlighting}{Verbatim}{
  frame=none,
  framesep=1pt,
  commandchars=\\\{\}
  % samepage=true  ← REMOVED to allow page breaks
}

% In Shaded environment:
\newenvironment{Shaded}{%
  \begin{shaded}%      % ← Uses 'shaded' instead of 'snugshade'
  % ... content ...
}{%
  \end{shaded}%
}
```
- **Current:** Code blocks **CAN** break across columns and pages
- **Alternative:** Use `snugshade` and `samepage=true` to prevent breaking
- **Benefits of breaking:**
  - Better space utilization in multi-column layout
  - Prevents large gaps when code blocks don't fit
  - More natural text flow
- **Trade-offs:**
  - Code blocks may be split at awkward points
  - Background shading continues across breaks

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
\vspace{0pt}%                   % No top padding
\fontsize{9}{10}\selectfont%    % Smaller font
\setlength{\leftskip}{0.5em}%   % Minimal margins
\setlength{\rightskip}{0.5em}%
% ...
\vspace{0pt}%                   % No bottom padding
```

#### Make Code Blocks More Readable
```latex
\vspace{0.3\baselineskip}%      % More top padding
\fontsize{11}{12.5}\selectfont% % Larger font with generous line spacing
\setlength{\leftskip}{1.5em}%   % Wider margins
\setlength{\rightskip}{1.5em}%
% ...
\vspace{0.1\baselineskip}%      % Minimal bottom padding
```

#### Remove Code Block Margins (Full Width)
```latex
\setlength{\leftskip}{0pt}%     % No left margin
\setlength{\rightskip}{0pt}%    % No right margin
```

#### Align with Different Text Elements
```latex
\setlength{\leftskip}{2em}%     % Match list indentation
\setlength{\rightskip}{1em}%    % Standard right margin
```
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

## Troubleshooting

### Font Size Changes Not Taking Effect

**Problem:** Changing `\fontsize{}{}` in the `Shaded` environment has no visible effect on code block text size.

**Cause:** The `Highlighting` environment (used for syntax highlighting inside `Shaded`) has a hardcoded `fontsize=\scriptsize` parameter that overrides the `Shaded` environment's font setting.

**Solution:** Remove the `fontsize=\scriptsize` line from the `Highlighting` environment definition:

```latex
% BEFORE (broken):
\DefineVerbatimEnvironment{Highlighting}{Verbatim}{
  fontsize=\scriptsize,    % ← This overrides Shaded font size
  frame=none,
  framesep=1pt,
  commandchars=\\\{\},
  samepage=true
}

% AFTER (fixed):
\DefineVerbatimEnvironment{Highlighting}{Verbatim}{
  frame=none,              % ← fontsize line removed, samepage removed
  framesep=1pt,
  commandchars=\\\{\}      % ← samepage=true also removed for page breaking
}
```

**Location:** This fix needs to be applied around lines 28-34 in both template files.

**Verification:** After making this change, regenerate PDFs and the font size in the `Shaded` environment should now take effect.

### Code Blocks Breaking Across Columns/Pages

**Current Behavior:** Code blocks can break across columns and pages for better space utilization.

**To PREVENT breaking (keep code blocks together):**

1. **Add `samepage=true` to Highlighting environment:**
```latex
\DefineVerbatimEnvironment{Highlighting}{Verbatim}{
  frame=none,
  framesep=1pt,
  commandchars=\\\{\},
  samepage=true              % ← Add this line
}
```

2. **Use `snugshade` instead of `shaded`:**
```latex
\newenvironment{Shaded}{%
  \begin{snugshade}%         % ← Change from 'shaded' to 'snugshade'
  % ... rest of environment ...
}{%
  \end{snugshade}%
}
```

**To ALLOW breaking (current configuration):**
- Use `shaded` environment (allows page breaks)
- Remove `samepage=true` from Highlighting environment
- Results in better space utilization but may split code blocks

## Additional Features
- ✅ Centered title with right-aligned version info
- ✅ Page numbers in footer
- ✅ Orphan/widow control for better page breaks
- ✅ `multicols*` environment prevents unwanted page breaks
- ✅ Optimized column separation and text flow
- ✅ Complete pandoc syntax highlighting support
- ✅ Code blocks can break across columns and pages
- ✅ Code blocks have left/right margins matching prose text
- ✅ Reduced padding for more compact layout
