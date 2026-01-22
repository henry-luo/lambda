# LaTeX Command Tracking

This document tracks all TeX/LaTeX commands currently implemented or being worked on in Lambda's LaTeX pipeline. Commands are grouped by package, starting from core TeX primitives to specialized packages.

**Status Legend:**
- ✅ **Full** - Fully implemented and tested
- 🔶 **Partial** - Basic implementation, some features missing
- ❌ **Missing** - Defined but not yet implemented

**Implementation Files:**
- Package definitions: `lambda/tex/packages/*.pkg.json`
- Package loader: `lambda/tex/tex_package_loader.cpp`
- Document model: `lambda/tex/tex_doc_model_*.cpp`
- Math typesetting: `lambda/tex/tex_math_*.cpp`
- HTML rendering: `lambda/tex/tex_html_render.cpp`

---

## 1. TeX Base

**Package:** `tex_base.pkg.json`  
**Implementation:** `tex_hlist.cpp`, `tex_vlist.cpp`, `tex_node.cpp`  
**Description:** Core TeX primitive commands that form the foundation of all typesetting operations.

### Grouping & Structure

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\relax` | Do nothing | ✅ Full | `tex_base.pkg.json` |
| `\ignorespaces` | Ignore following spaces | ✅ Full | `tex_base.pkg.json` |
| `\begingroup` | Begin a group | ✅ Full | `tex_base.pkg.json` |
| `\endgroup` | End a group | ✅ Full | `tex_base.pkg.json` |
| `\bgroup` | Begin group (alias) | ✅ Full | `tex_base.pkg.json` |
| `\egroup` | End group (alias) | ✅ Full | `tex_base.pkg.json` |

### Paragraph Control

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\par` | End paragraph | ✅ Full | `tex_base.pkg.json`, `tex_doc_model_text.cpp` |
| `\indent` | Begin paragraph with indentation | ✅ Full | `tex_base.pkg.json` |
| `\noindent` | Begin paragraph without indentation | ✅ Full | `tex_base.pkg.json` |

### Spacing & Glue

| Command  | Description                   | Status | Implementation |
| -------- | ----------------------------- | ------ | -------------- |
| `\hskip` | Horizontal skip               | ✅ Full | `tex_base.pkg.json`, `tex_hlist.cpp` |
| `\vskip` | Vertical skip                 | ✅ Full | `tex_base.pkg.json`, `tex_vlist.cpp` |
| `\kern`  | Insert kern (fixed space)     | ✅ Full | `tex_base.pkg.json`, `tex_hlist.cpp` |
| `\hfil`  | Horizontal fill (order 1)     | ✅ Full | `tex_base.pkg.json` |
| `\hfill` | Horizontal fill (order 2)     | ✅ Full | `tex_base.pkg.json` |
| `\hss`   | Horizontal stretch and shrink | ✅ Full | `tex_base.pkg.json` |
| `\vfil`  | Vertical fill (order 1)       | ✅ Full | `tex_base.pkg.json` |
| `\vfill` | Vertical fill (order 2)       | ✅ Full | `tex_base.pkg.json` |
| `\vss`   | Vertical stretch and shrink   | ✅ Full | `tex_base.pkg.json` |

### Rules

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\hrule` | Horizontal rule | ✅ Full | `tex_base.pkg.json`, `tex_node.cpp` |
| `\vrule` | Vertical rule | ✅ Full | `tex_base.pkg.json`, `tex_node.cpp` |

### Penalties & Breaking

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\penalty` | Insert penalty | ✅ Full | `tex_base.pkg.json` |
| `\break` | Force break (penalty -10000) | ✅ Full | `tex_base.pkg.json` |
| `\nobreak` | Prevent break (penalty 10000) | ✅ Full | `tex_base.pkg.json` |
| `\allowbreak` | Allow break (penalty 0) | ✅ Full | `tex_base.pkg.json` |

### Boxes

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\hbox` | Horizontal box | ✅ Full | `tex_base.pkg.json`, `tex_hlist.cpp` |
| `\vbox` | Vertical box | ✅ Full | `tex_base.pkg.json`, `tex_vlist.cpp` |
| `\vtop` | Vertical box aligned at top | ✅ Full | `tex_base.pkg.json`, `tex_vlist.cpp` |
| `\raise` | Raise box | ✅ Full | `tex_base.pkg.json` |
| `\lower` | Lower box | ✅ Full | `tex_base.pkg.json` |
| `\moveleft` | Move box left | 🔶 Partial | `tex_base.pkg.json` |
| `\moveright` | Move box right | 🔶 Partial | `tex_base.pkg.json` |
| `\rlap` | Right overlap (zero-width) | ✅ Full | `tex_base.pkg.json` |
| `\llap` | Left overlap (zero-width) | ✅ Full | `tex_base.pkg.json` |

### Output & I/O

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\special` | Pass special command to output | 🔶 Partial | `tex_base.pkg.json` |
| `\write` | Write to file | ❌ Missing | — |
| `\message` | Print message to terminal | ❌ Missing | — |
| `\mark` | Insert mark (for headers/footers) | ❌ Missing | — |
| `\insert` | Insert floating material | ❌ Missing | — |

### Characters

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\char` | Insert character by code | ✅ Full | `tex_base.pkg.json` |
| `\accent` | Put accent over character | ✅ Full | `tex_base.pkg.json` |

---

## 2. LaTeX Base

**Package:** `latex_base.pkg.json`  
**Implementation:** `tex_doc_model_struct.cpp`, `tex_doc_model_inline.cpp`, `tex_doc_model_commands.cpp`  
**Description:** Standard LaTeX commands built on top of TeX primitives.

### Document Structure

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\documentclass` | Document class declaration | ✅ Full | `latex_base.pkg.json`, `tex_package_loader.cpp` |
| `\usepackage` | Load package | ✅ Full | `latex_base.pkg.json`, `tex_package_loader.cpp` |
| `\begin{document}` | Start document body | ✅ Full | `latex_base.pkg.json` |
| `\end{document}` | End document body | ✅ Full | `latex_base.pkg.json` |

### Sectioning

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\part` | Part heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\part*` | Unnumbered part | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\chapter` | Chapter heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\chapter*` | Unnumbered chapter | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\section` | Section heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\section*` | Unnumbered section | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\subsection` | Subsection heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\subsection*` | Unnumbered subsection | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\subsubsection` | Subsubsection heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\subsubsection*` | Unnumbered subsubsection | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\paragraph` | Paragraph heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\subparagraph` | Subparagraph heading | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |

### Text Formatting

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\textbf` | Bold text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textit` | Italic text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\texttt` | Monospace text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textrm` | Roman text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textsf` | Sans-serif text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textsc` | Small caps text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textsl` | Slanted text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textup` | Upright text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\textnormal` | Normal text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\emph` | Emphasized text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |
| `\underline` | Underlined text | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_inline.cpp` |

### Font Switches

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\bfseries` | Switch to bold | ✅ Full | `latex_base.pkg.json` |
| `\mdseries` | Switch to medium weight | ✅ Full | `latex_base.pkg.json` |
| `\itshape` | Switch to italic | ✅ Full | `latex_base.pkg.json` |
| `\upshape` | Switch to upright | ✅ Full | `latex_base.pkg.json` |
| `\slshape` | Switch to slanted | ✅ Full | `latex_base.pkg.json` |
| `\scshape` | Switch to small caps | ✅ Full | `latex_base.pkg.json` |
| `\ttfamily` | Switch to monospace | ✅ Full | `latex_base.pkg.json` |
| `\rmfamily` | Switch to roman | ✅ Full | `latex_base.pkg.json` |
| `\sffamily` | Switch to sans-serif | ✅ Full | `latex_base.pkg.json` |
| `\normalfont` | Switch to normal font | ✅ Full | `latex_base.pkg.json` |

### Font Sizes

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\tiny` | Tiny font size (5pt) | ✅ Full | `latex_base.pkg.json` |
| `\scriptsize` | Script font size (7pt) | ✅ Full | `latex_base.pkg.json` |
| `\footnotesize` | Footnote size (8pt) | ✅ Full | `latex_base.pkg.json` |
| `\small` | Small size (9pt) | ✅ Full | `latex_base.pkg.json` |
| `\normalsize` | Normal size (10pt) | ✅ Full | `latex_base.pkg.json` |
| `\large` | Large size (12pt) | ✅ Full | `latex_base.pkg.json` |
| `\Large` | Larger size (14pt) | ✅ Full | `latex_base.pkg.json` |
| `\LARGE` | Very large size (17pt) | ✅ Full | `latex_base.pkg.json` |
| `\huge` | Huge size (20pt) | ✅ Full | `latex_base.pkg.json` |
| `\Huge` | Very huge size (25pt) | ✅ Full | `latex_base.pkg.json` |

### Cross-References

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\label` | Set label for cross-reference | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_commands.cpp` |
| `\ref` | Reference to label | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_commands.cpp` |
| `\pageref` | Page reference to label | 🔶 Partial | `latex_base.pkg.json` |
| `\eqref` | Equation reference (with parentheses) | ✅ Full | `latex_base.pkg.json` |

### Footnotes & Notes

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\footnote` | Footnote | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\footnotemark` | Footnote mark only | ✅ Full | `latex_base.pkg.json` |
| `\footnotetext` | Footnote text only | ✅ Full | `latex_base.pkg.json` |

### Lists

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\item` | List item | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\begin{itemize}` | Bullet list | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\begin{enumerate}` | Numbered list | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\begin{description}` | Description list | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |

### Floats & Captions

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\caption` | Float caption | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\begin{figure}` | Figure environment | ✅ Full | `latex_base.pkg.json` |
| `\begin{figure*}` | Wide figure | ✅ Full | `latex_base.pkg.json` |
| `\begin{table}` | Table environment | ✅ Full | `latex_base.pkg.json` |
| `\begin{table*}` | Wide table | ✅ Full | `latex_base.pkg.json` |

### Title & Abstract

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\title` | Document title | ✅ Full | `latex_base.pkg.json` |
| `\author` | Document author | ✅ Full | `latex_base.pkg.json` |
| `\date` | Document date | ✅ Full | `latex_base.pkg.json` |
| `\thanks` | Author thanks | ✅ Full | `latex_base.pkg.json` |
| `\maketitle` | Generate title block | ✅ Full | `latex_base.pkg.json`, `tex_doc_model_struct.cpp` |
| `\begin{abstract}` | Abstract environment | ✅ Full | `latex_base.pkg.json` |

### Table of Contents

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\tableofcontents` | Table of contents | 🔶 Partial | `latex_base.pkg.json` |
| `\listoffigures` | List of figures | 🔶 Partial | `latex_base.pkg.json` |
| `\listoftables` | List of tables | 🔶 Partial | `latex_base.pkg.json` |

### Spacing

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\hspace` | Horizontal space | ✅ Full | `latex_base.pkg.json` |
| `\hspace*` | Horizontal space (preserved) | ✅ Full | `latex_base.pkg.json` |
| `\vspace` | Vertical space | ✅ Full | `latex_base.pkg.json` |
| `\vspace*` | Vertical space (preserved) | ✅ Full | `latex_base.pkg.json` |
| `\quad` | Em space | ✅ Full | `latex_base.pkg.json` |
| `\qquad` | Two em spaces | ✅ Full | `latex_base.pkg.json` |
| `\enspace` | En space (0.5em) | ✅ Full | `latex_base.pkg.json` |
| `\thinspace` | Thin space | ✅ Full | `latex_base.pkg.json` |
| `\negthinspace` | Negative thin space | ✅ Full | `latex_base.pkg.json` |
| `\,` | Thin space (math) | ✅ Full | `latex_base.pkg.json` |
| `\:` | Medium space (math) | ✅ Full | `latex_base.pkg.json` |
| `\;` | Thick space (math) | ✅ Full | `latex_base.pkg.json` |
| `\!` | Negative thin space | ✅ Full | `latex_base.pkg.json` |
| `\ ` | Control space | ✅ Full | `latex_base.pkg.json` |
| `~` | Non-breaking space | ✅ Full | `latex_base.pkg.json` |

### Page & Line Breaking

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\newline` | New line | ✅ Full | `latex_base.pkg.json` |
| `\\` | Line break | ✅ Full | `latex_base.pkg.json`, `tex_linebreak.cpp` |
| `\linebreak` | Line break with optional penalty | ✅ Full | `latex_base.pkg.json` |
| `\nolinebreak` | Prevent line break | ✅ Full | `latex_base.pkg.json` |
| `\pagebreak` | Page break | ✅ Full | `latex_base.pkg.json`, `tex_pagebreak.cpp` |
| `\nopagebreak` | Prevent page break | ✅ Full | `latex_base.pkg.json` |
| `\newpage` | New page | ✅ Full | `latex_base.pkg.json` |
| `\clearpage` | Clear page and flush floats | ✅ Full | `latex_base.pkg.json` |
| `\cleardoublepage` | Clear to next odd page | 🔶 Partial | `latex_base.pkg.json` |

### Alignment

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\centering` | Center text in environment | ✅ Full | `latex_base.pkg.json` |
| `\raggedright` | Left-align text | ✅ Full | `latex_base.pkg.json` |
| `\raggedleft` | Right-align text | ✅ Full | `latex_base.pkg.json` |
| `\begin{center}` | Center environment | ✅ Full | `latex_base.pkg.json` |
| `\begin{flushleft}` | Left-align environment | ✅ Full | `latex_base.pkg.json` |
| `\begin{flushright}` | Right-align environment | ✅ Full | `latex_base.pkg.json` |

### Special Characters

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\%` | Percent character | ✅ Full | `latex_base.pkg.json` |
| `\&` | Ampersand character | ✅ Full | `latex_base.pkg.json` |
| `\#` | Hash character | ✅ Full | `latex_base.pkg.json` |
| `\$` | Dollar character | ✅ Full | `latex_base.pkg.json` |
| `\_` | Underscore character | ✅ Full | `latex_base.pkg.json` |
| `\{` | Left brace character | ✅ Full | `latex_base.pkg.json` |
| `\}` | Right brace character | ✅ Full | `latex_base.pkg.json` |
| `\textbackslash` | Backslash | ✅ Full | `latex_base.pkg.json` |

### Logos & Symbols

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\TeX` | TeX logo | ✅ Full | `latex_base.pkg.json` |
| `\LaTeX` | LaTeX logo | ✅ Full | `latex_base.pkg.json` |
| `\LaTeXe` | LaTeX2ε logo | ✅ Full | `latex_base.pkg.json` |
| `\today` | Current date | ✅ Full | `latex_base.pkg.json` |
| `\ldots` | Ellipsis | ✅ Full | `latex_base.pkg.json` |
| `\dots` | Ellipsis (alias) | ✅ Full | `latex_base.pkg.json` |
| `\dag` | Dagger † | ✅ Full | `latex_base.pkg.json` |
| `\ddag` | Double dagger ‡ | ✅ Full | `latex_base.pkg.json` |
| `\S` | Section sign § | ✅ Full | `latex_base.pkg.json` |
| `\P` | Pilcrow ¶ | ✅ Full | `latex_base.pkg.json` |
| `\copyright` | Copyright © | ✅ Full | `latex_base.pkg.json` |
| `\textregistered` | Registered ® | ✅ Full | `latex_base.pkg.json` |
| `\texttrademark` | Trademark ™ | ✅ Full | `latex_base.pkg.json` |
| `\pounds` | Pound sign £ | ✅ Full | `latex_base.pkg.json` |

### Accents (Text Mode)

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `` \` `` | Grave accent (è) | ✅ Full | `latex_base.pkg.json` |
| `\'` | Acute accent (é) | ✅ Full | `latex_base.pkg.json` |
| `\^` | Circumflex (ê) | ✅ Full | `latex_base.pkg.json` |
| `\"` | Umlaut (ë) | ✅ Full | `latex_base.pkg.json` |
| `\~` | Tilde (ñ) | ✅ Full | `latex_base.pkg.json` |
| `\=` | Macron (ā) | ✅ Full | `latex_base.pkg.json` |
| `\.` | Dot accent (ȧ) | ✅ Full | `latex_base.pkg.json` |
| `\c` | Cedilla (ç) | ✅ Full | `latex_base.pkg.json` |
| `\v` | Háček (č) | ✅ Full | `latex_base.pkg.json` |
| `\u` | Breve (ă) | ✅ Full | `latex_base.pkg.json` |
| `\H` | Hungarian umlaut (ő) | ✅ Full | `latex_base.pkg.json` |
| `\r` | Ring (å) | ✅ Full | `latex_base.pkg.json` |
| `\t` | Tie accent | ✅ Full | `latex_base.pkg.json` |
| `\d` | Dot below (ạ) | ✅ Full | `latex_base.pkg.json` |
| `\b` | Bar below | ✅ Full | `latex_base.pkg.json` |

### Verbatim

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\verb` | Verbatim text | ✅ Full | `latex_base.pkg.json` |
| `\verb*` | Verbatim with visible spaces | ✅ Full | `latex_base.pkg.json` |
| `\begin{verbatim}` | Verbatim environment | ✅ Full | `latex_base.pkg.json` |
| `\begin{verbatim*}` | Verbatim with visible spaces | ✅ Full | `latex_base.pkg.json` |

### Macro Definition

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\newcommand` | Define new command | ✅ Full | `latex_base.pkg.json`, `tex_command_registry.cpp` |
| `\renewcommand` | Redefine command | ✅ Full | `latex_base.pkg.json`, `tex_command_registry.cpp` |
| `\providecommand` | Define if not exists | ✅ Full | `latex_base.pkg.json`, `tex_command_registry.cpp` |
| `\newenvironment` | Define new environment | ✅ Full | `latex_base.pkg.json` |
| `\renewenvironment` | Redefine environment | ✅ Full | `latex_base.pkg.json` |

### Counters

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\newcounter` | Define new counter | ✅ Full | `latex_base.pkg.json` |
| `\setcounter` | Set counter value | ✅ Full | `latex_base.pkg.json` |
| `\addtocounter` | Add to counter | ✅ Full | `latex_base.pkg.json` |
| `\stepcounter` | Increment counter | ✅ Full | `latex_base.pkg.json` |
| `\refstepcounter` | Increment counter and set ref | ✅ Full | `latex_base.pkg.json` |
| `\arabic` | Counter as arabic number | ✅ Full | `latex_base.pkg.json` |
| `\roman` | Counter as lowercase roman | ✅ Full | `latex_base.pkg.json` |
| `\Roman` | Counter as uppercase roman | ✅ Full | `latex_base.pkg.json` |
| `\alph` | Counter as lowercase letter | ✅ Full | `latex_base.pkg.json` |
| `\Alph` | Counter as uppercase letter | ✅ Full | `latex_base.pkg.json` |
| `\fnsymbol` | Counter as footnote symbol | ✅ Full | `latex_base.pkg.json` |

### Tables (Basic)

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\begin{tabular}` | Table environment | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |
| `\begin{tabular*}` | Table with specified width | ✅ Full | `latex_base.pkg.json` |
| `\hline` | Horizontal line | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |
| `\cline` | Partial horizontal line | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |
| `\multicolumn` | Span multiple columns | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |
| `\multirow` | Span multiple rows | ✅ Full | `latex_base.pkg.json` |
| `\begin{array}` | Math array | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |

### Quotations

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\begin{quote}` | Short quotation | ✅ Full | `latex_base.pkg.json` |
| `\begin{quotation}` | Long quotation | ✅ Full | `latex_base.pkg.json` |
| `\begin{verse}` | Verse environment | ✅ Full | `latex_base.pkg.json` |

### Math Environments (Basic)

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\begin{equation}` | Numbered equation | ✅ Full | `latex_base.pkg.json`, `tex_math_bridge.cpp` |
| `\begin{equation*}` | Unnumbered equation | ✅ Full | `latex_base.pkg.json`, `tex_math_bridge.cpp` |
| `\begin{displaymath}` | Display math | ✅ Full | `latex_base.pkg.json` |
| `\begin{math}` | Inline math | ✅ Full | `latex_base.pkg.json` |
| `$...$` | Inline math | ✅ Full | `tex_math_bridge.cpp` |
| `$$...$$` | Display math | ✅ Full | `tex_math_bridge.cpp` |
| `\[...\]` | Display math | ✅ Full | `tex_math_bridge.cpp` |
| `\(...\)` | Inline math | ✅ Full | `tex_math_bridge.cpp` |
| `\begin{eqnarray}` | Equation array | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |
| `\begin{eqnarray*}` | Unnumbered equation array | ✅ Full | `latex_base.pkg.json`, `tex_align.cpp` |

### Boxes

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\begin{minipage}` | Minipage | ✅ Full | `latex_base.pkg.json` |
| `\parbox` | Paragraph box | ✅ Full | `latex_base.pkg.json` |
| `\mbox` | Horizontal box | ✅ Full | `latex_base.pkg.json` |
| `\makebox` | Box with specified width | ✅ Full | `latex_base.pkg.json` |
| `\fbox` | Framed box | ✅ Full | `latex_base.pkg.json` |
| `\framebox` | Framed box with width | ✅ Full | `latex_base.pkg.json` |
| `\raisebox` | Raised box | ✅ Full | `latex_base.pkg.json` |

### Bibliography

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\begin{thebibliography}` | Bibliography environment | ✅ Full | `latex_base.pkg.json` |
| `\bibitem` | Bibliography item | ✅ Full | `latex_base.pkg.json` |
| `\cite` | Citation | ✅ Full | `latex_base.pkg.json` |

---

## 3. AMS Math

**Package:** `amsmath.pkg.json`  
**Implementation:** `tex_math_ast_typeset.cpp`, `tex_align.cpp`  
**Description:** American Mathematical Society extensions for professional mathematical typesetting.

### Fractions

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\frac` | Fraction | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\dfrac` | Display-style fraction | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\tfrac` | Text-style fraction | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\cfrac` | Continued fraction | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\genfrac` | Generalized fraction | ✅ Full | `amsmath.pkg.json` |

### Binomials

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\binom` | Binomial coefficient | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\dbinom` | Display-style binomial | ✅ Full | `amsmath.pkg.json` |
| `\tbinom` | Text-style binomial | ✅ Full | `amsmath.pkg.json` |

### Roots & Radicals

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\sqrt` | Square/nth root | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |

### Text in Math

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\text` | Text in math | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\intertext` | Text between align rows | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `\shortintertext` | Short intertext | ✅ Full | `amsmath.pkg.json` |

### Operators

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\operatorname` | Named operator | ✅ Full | `amsmath.pkg.json` |
| `\operatorname*` | Named operator with limits | ✅ Full | `amsmath.pkg.json` |
| `\DeclareMathOperator` | Declare math operator | ✅ Full | `amsmath.pkg.json` |
| `\DeclareMathOperator*` | Declare with limits | ✅ Full | `amsmath.pkg.json` |

### Decorations

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\boldsymbol` | Bold symbol | ✅ Full | `amsmath.pkg.json` |
| `\pmb` | Poor man's bold | ✅ Full | `amsmath.pkg.json` |
| `\overset` | Symbol with accent above | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\underset` | Symbol with accent below | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\stackrel` | Symbol with accent (deprecated) | ✅ Full | `amsmath.pkg.json` |
| `\sideset` | Side scripts on large operator | ✅ Full | `amsmath.pkg.json` |
| `\substack` | Stacked subscripts/superscripts | ✅ Full | `amsmath.pkg.json` |

### Extensible Arrows

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\xleftarrow` | Left arrow with text | ✅ Full | `amsmath.pkg.json` |
| `\xrightarrow` | Right arrow with text | ✅ Full | `amsmath.pkg.json` |

### Tags & Numbering

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\tag` | Custom equation tag | ✅ Full | `amsmath.pkg.json` |
| `\tag*` | Custom tag (no parens) | ✅ Full | `amsmath.pkg.json` |
| `\notag` | Suppress equation number | ✅ Full | `amsmath.pkg.json` |
| `\nonumber` | Suppress equation number (alias) | ✅ Full | `amsmath.pkg.json` |
| `\numberwithin` | Reset counter within | ✅ Full | `amsmath.pkg.json` |

### Boxing

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\boxed` | Boxed equation | ✅ Full | `amsmath.pkg.json` |

### Modular Arithmetic

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\mod` | Modulo | ✅ Full | `amsmath.pkg.json` |
| `\bmod` | Binary modulo | ✅ Full | `amsmath.pkg.json` |
| `\pmod` | Parenthesized modulo | ✅ Full | `amsmath.pkg.json` |
| `\pod` | Parenthesized | ✅ Full | `amsmath.pkg.json` |

### Multi-line Environments

| Environment | Description | Status | Implementation |
|-------------|-------------|--------|----------------|
| `align` | Aligned equations (numbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `align*` | Aligned equations (unnumbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `alignat` | Aligned with column count | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `alignat*` | Alignat unnumbered | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `aligned` | Aligned subequations | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `alignedat` | Alignedat with columns | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `gather` | Gathered equations (numbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `gather*` | Gathered equations (unnumbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `gathered` | Gathered subequations | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `multline` | Multi-line equation (numbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `multline*` | Multi-line equation (unnumbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `flalign` | Full-width aligned | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `flalign*` | Full-width aligned (unnumbered) | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `split` | Split equation | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |

### Cases

| Environment | Description | Status | Implementation |
|-------------|-------------|--------|----------------|
| `cases` | Cases | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `dcases` | Display-style cases | ✅ Full | `amsmath.pkg.json` |
| `rcases` | Right-side cases | ✅ Full | `amsmath.pkg.json` |
| `drcases` | Display right-side cases | ✅ Full | `amsmath.pkg.json` |

### Matrices

| Environment | Description | Status | Implementation |
|-------------|-------------|--------|----------------|
| `matrix` | Plain matrix | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `pmatrix` | Matrix with parentheses | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `bmatrix` | Matrix with brackets | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `Bmatrix` | Matrix with braces | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `vmatrix` | Matrix with vertical bars | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `Vmatrix` | Matrix with double bars | ✅ Full | `amsmath.pkg.json`, `tex_align.cpp` |
| `smallmatrix` | Small inline matrix | ✅ Full | `amsmath.pkg.json` |
| `subarray` | Stacked subscript array | ✅ Full | `amsmath.pkg.json` |

### Math Functions

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\sin`, `\cos`, `\tan` | Trigonometric | ✅ Full | `amsmath.pkg.json` |
| `\sec`, `\csc`, `\cot` | Trigonometric | ✅ Full | `amsmath.pkg.json` |
| `\arcsin`, `\arccos`, `\arctan` | Inverse trig | ✅ Full | `amsmath.pkg.json` |
| `\sinh`, `\cosh`, `\tanh`, `\coth` | Hyperbolic | ✅ Full | `amsmath.pkg.json` |
| `\log`, `\ln`, `\lg`, `\exp` | Logarithmic | ✅ Full | `amsmath.pkg.json` |
| `\lim`, `\limsup`, `\liminf` | Limits | ✅ Full | `amsmath.pkg.json` |
| `\max`, `\min`, `\sup`, `\inf` | Extrema | ✅ Full | `amsmath.pkg.json` |
| `\arg`, `\det`, `\dim`, `\hom`, `\ker` | Various | ✅ Full | `amsmath.pkg.json` |
| `\deg`, `\gcd`, `\Pr` | Various | ✅ Full | `amsmath.pkg.json` |

### Integrals

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\int` | Integral | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\iint` | Double integral | ✅ Full | `amsmath.pkg.json` |
| `\iiint` | Triple integral | ✅ Full | `amsmath.pkg.json` |
| `\iiiint` | Quadruple integral | ✅ Full | `amsmath.pkg.json` |
| `\idotsint` | Multiple integral with dots | ✅ Full | `amsmath.pkg.json` |
| `\oint` | Contour integral | ✅ Full | `amsmath.pkg.json` |
| `\oiint` | Surface integral | ✅ Full | `esint.pkg.json` |
| `\oiiint` | Volume integral | ✅ Full | `esint.pkg.json` |

### Big Operators

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\sum`, `\prod`, `\coprod` | Sum/Product | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\bigcup`, `\bigcap` | Set operations | ✅ Full | `amsmath.pkg.json` |
| `\bigsqcup` | Square union | ✅ Full | `amsmath.pkg.json` |
| `\bigvee`, `\bigwedge` | Logical | ✅ Full | `amsmath.pkg.json` |
| `\bigodot`, `\bigoplus`, `\bigotimes` | Circled | ✅ Full | `amsmath.pkg.json` |
| `\biguplus` | Multiset union | ✅ Full | `amsmath.pkg.json` |

### Delimiters

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\left`, `\right` | Auto-sizing delimiters | ✅ Full | `amsmath.pkg.json`, `tex_math_ast_typeset.cpp` |
| `\middle` | Auto-sizing middle delimiter | ✅ Full | `amsmath.pkg.json` |
| `\big`, `\Big`, `\bigg`, `\Bigg` | Manual sizing | ✅ Full | `amsmath.pkg.json` |
| `\bigl`, `\Bigl`, `\biggl`, `\Biggl` | Left delimiters | ✅ Full | `amsmath.pkg.json` |
| `\bigr`, `\Bigr`, `\biggr`, `\Biggr` | Right delimiters | ✅ Full | `amsmath.pkg.json` |
| `\bigm`, `\Bigm`, `\biggm`, `\Biggm` | Middle delimiters | ✅ Full | `amsmath.pkg.json` |

### Greek Letters

| Commands | Status | Implementation |
|----------|--------|----------------|
| `\alpha` through `\omega` (lowercase) | ✅ Full | `amsmath.pkg.json` |
| `\Gamma` through `\Omega` (uppercase) | ✅ Full | `amsmath.pkg.json` |
| Variant forms (`\varepsilon`, `\vartheta`, etc.) | ✅ Full | `amsmath.pkg.json` |

### Arrows

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\to`, `\gets` | Basic arrows | ✅ Full | `amsmath.pkg.json` |
| `\rightarrow`, `\leftarrow` | Arrows | ✅ Full | `amsmath.pkg.json` |
| `\Rightarrow`, `\Leftarrow` | Double arrows | ✅ Full | `amsmath.pkg.json` |
| `\leftrightarrow`, `\Leftrightarrow` | Double-headed | ✅ Full | `amsmath.pkg.json` |
| `\uparrow`, `\downarrow` | Vertical arrows | ✅ Full | `amsmath.pkg.json` |
| `\mapsto`, `\longmapsto` | Maps to | ✅ Full | `amsmath.pkg.json` |
| `\implies`, `\impliedby`, `\iff` | Logical | ✅ Full | `amsmath.pkg.json` |
| Long arrow variants | | ✅ Full | `amsmath.pkg.json` |
| Hook and harpoon arrows | | ✅ Full | `amsmath.pkg.json` |

### Relations

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\leq`, `\geq`, `\neq` | Comparisons | ✅ Full | `amsmath.pkg.json` |
| `\ll`, `\gg` | Much less/greater | ✅ Full | `amsmath.pkg.json` |
| `\sim`, `\simeq`, `\cong`, `\approx` | Similarities | ✅ Full | `amsmath.pkg.json` |
| `\equiv`, `\propto` | Equivalence | ✅ Full | `amsmath.pkg.json` |
| `\prec`, `\succ`, `\preceq`, `\succeq` | Precedence | ✅ Full | `amsmath.pkg.json` |
| `\subset`, `\supset`, `\subseteq`, `\supseteq` | Subsets | ✅ Full | `amsmath.pkg.json` |
| `\in`, `\ni`, `\notin` | Membership | ✅ Full | `amsmath.pkg.json` |
| `\mid`, `\parallel`, `\perp` | Geometric | ✅ Full | `amsmath.pkg.json` |
| `\vdash`, `\dashv`, `\models` | Turnstiles | ✅ Full | `amsmath.pkg.json` |

### Binary Operations

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\pm`, `\mp` | Plus/minus | ✅ Full | `amsmath.pkg.json` |
| `\times`, `\div`, `\cdot` | Multiplication/division | ✅ Full | `amsmath.pkg.json` |
| `\oplus`, `\ominus`, `\otimes`, `\oslash`, `\odot` | Circled | ✅ Full | `amsmath.pkg.json` |
| `\wedge`, `\vee` (or `\land`, `\lor`) | Logical | ✅ Full | `amsmath.pkg.json` |
| `\cap`, `\cup`, `\sqcap`, `\sqcup` | Set operations | ✅ Full | `amsmath.pkg.json` |
| `\setminus` | Set difference | ✅ Full | `amsmath.pkg.json` |

### Miscellaneous Symbols

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\forall`, `\exists`, `\nexists` | Quantifiers | ✅ Full | `amsmath.pkg.json` |
| `\neg` / `\lnot` | Negation | ✅ Full | `amsmath.pkg.json` |
| `\emptyset`, `\varnothing` | Empty set | ✅ Full | `amsmath.pkg.json` |
| `\infty`, `\nabla`, `\partial` | Calculus | ✅ Full | `amsmath.pkg.json` |
| `\aleph`, `\beth`, `\hbar`, `\ell` | Special | ✅ Full | `amsmath.pkg.json` |
| `\wp`, `\Re`, `\Im` | Special functions | ✅ Full | `amsmath.pkg.json` |
| `\angle`, `\triangle`, `\square`, `\diamond` | Shapes | ✅ Full | `amsmath.pkg.json` |
| `\prime`, `\backprime` | Primes | ✅ Full | `amsmath.pkg.json` |
| `\cdots`, `\ddots`, `\vdots`, `\ldots` | Dots | ✅ Full | `amsmath.pkg.json` |

### Bracket Delimiters

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\lvert`, `\rvert` | Vertical bars | ✅ Full | `amsmath.pkg.json` |
| `\lVert`, `\rVert` | Double vertical bars | ✅ Full | `amsmath.pkg.json` |
| `\langle`, `\rangle` | Angle brackets | ✅ Full | `amsmath.pkg.json` |
| `\lceil`, `\rceil` | Ceiling | ✅ Full | `amsmath.pkg.json` |
| `\lfloor`, `\rfloor` | Floor | ✅ Full | `amsmath.pkg.json` |
| `\lbrace`, `\rbrace` | Braces | ✅ Full | `amsmath.pkg.json` |
| `\lbrack`, `\rbrack` | Brackets | ✅ Full | `amsmath.pkg.json` |

---

## 4. AMS Symbols

**Package:** `amssymb.pkg.json`  
**Description:** Extended symbol collection from AMS.

| Category | Status | Implementation |
|----------|--------|----------------|
| Extended Relations (`\leqq`, `\geqq`, etc.) | ✅ Full | `amssymb.pkg.json` |
| Extended Binary Operations (`\divideontimes`, etc.) | ✅ Full | `amssymb.pkg.json` |
| Extended Arrows (`\twoheadleftarrow`, etc.) | ✅ Full | `amssymb.pkg.json` |
| Miscellaneous Symbols (`\therefore`, `\because`, etc.) | ✅ Full | `amssymb.pkg.json` |
| Font Commands (`\mathbb`, `\mathfrak`, `\mathscr`) | ✅ Full | `amssymb.pkg.json` |

---

## 5. AMS Theorem

**Package:** `amsthm.pkg.json`  
**Description:** Theorem-like environments with customizable styles.

| Command/Environment | Description | Status | Implementation |
|---------------------|-------------|--------|----------------|
| `\newtheorem` | Define theorem-like environment | ✅ Full | `amsthm.pkg.json` |
| `\theoremstyle` | Set style for theorem definitions | ✅ Full | `amsthm.pkg.json` |
| `\newtheoremstyle` | Define custom theorem style | 🔶 Partial | `amsthm.pkg.json` |
| `\swapnumbers` | Put numbers before theorem name | ❌ Missing | — |
| `\qed` | End of proof mark | ✅ Full | `amsthm.pkg.json` |
| `\qedhere` | Place QED at current location | ✅ Full | `amsthm.pkg.json` |
| `proof` environment | Proof environment | ✅ Full | `amsthm.pkg.json` |

---

## 6. Graphics

**Package:** `graphicx.pkg.json`  
**Implementation:** `tex_graphics.cpp`  
**Description:** Standard package for including images.

### Image Inclusion

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\includegraphics` | Include an image | ✅ Full | `graphicx.pkg.json`, `tex_graphics.cpp` |
| `\graphicspath` | Set image search paths | ✅ Full | `graphicx.pkg.json` |
| `\DeclareGraphicsExtensions` | Set file extensions | 🔶 Partial | `graphicx.pkg.json` |
| `\DeclareGraphicsRule` | Define graphics handling | ❌ Missing | — |

### Transformations

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\rotatebox` | Rotate content | ✅ Full | `graphicx.pkg.json`, `tex_graphics.cpp` |
| `\scalebox` | Scale content | ✅ Full | `graphicx.pkg.json`, `tex_graphics.cpp` |
| `\reflectbox` | Horizontally reflect content | ✅ Full | `graphicx.pkg.json` |
| `\resizebox` | Resize to specific dimensions | ✅ Full | `graphicx.pkg.json`, `tex_graphics.cpp` |
| `\resizebox*` | Resize (total height) | ✅ Full | `graphicx.pkg.json` |

---

## 7. Hyperref

**Package:** `hyperref.pkg.json`  
**Description:** Hypertext links and PDF metadata.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\href` | Hyperlink with text | ✅ Full | `hyperref.pkg.json` |
| `\url` | URL in typewriter font | ✅ Full | `hyperref.pkg.json`, `url.pkg.json` |
| `\nolinkurl` | URL without hyperlink | ✅ Full | `hyperref.pkg.json` |
| `\hyperref` | Internal reference with custom text | ✅ Full | `hyperref.pkg.json` |
| `\hyperlink` | Create link to anchor | ✅ Full | `hyperref.pkg.json` |
| `\hypertarget` | Create anchor | ✅ Full | `hyperref.pkg.json` |
| `\autoref` | Reference with auto-generated name | ✅ Full | `hyperref.pkg.json` |
| `\nameref` | Reference by section name | ✅ Full | `hyperref.pkg.json` |
| `\hypersetup` | Configure hyperref options | ✅ Full | `hyperref.pkg.json` |
| `\phantomsection` | Invisible anchor | ✅ Full | `hyperref.pkg.json` |
| `\texorpdfstring` | Different text for TeX/PDF | ✅ Full | `hyperref.pkg.json` |

---

## 8. Color Packages

### xcolor

**Package:** `xcolor.pkg.json`  
**Description:** Extended color support with multiple color models.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\color` | Switch to specified color | ✅ Full | `xcolor.pkg.json` |
| `\textcolor` | Typeset text in color | ✅ Full | `xcolor.pkg.json` |
| `\colorbox` | Box with colored background | ✅ Full | `xcolor.pkg.json` |
| `\fcolorbox` | Box with colored frame and background | ✅ Full | `xcolor.pkg.json` |
| `\pagecolor` | Set page background color | 🔶 Partial | `xcolor.pkg.json` |
| `\definecolor` | Define a new color | ✅ Full | `xcolor.pkg.json` |
| `\colorlet` | Define color as copy | ✅ Full | `xcolor.pkg.json` |
| `\rowcolors` | Alternate row colors in tables | 🔶 Partial | `xcolor.pkg.json` |

---

## 9. TikZ/PGF

**Package:** `tikz.pkg.json`  
**Implementation:** `tex_pgf_driver.cpp`  
**Description:** Create graphics programmatically.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\tikz` | Inline TikZ drawing | 🔶 Partial | `tikz.pkg.json`, `tex_pgf_driver.cpp` |
| `\tikzset` | Set TikZ options globally | 🔶 Partial | `tikz.pkg.json` |
| `\usetikzlibrary` | Load TikZ libraries | 🔶 Partial | `tikz.pkg.json` |
| `\draw` | Draw path | 🔶 Partial | `tikz.pkg.json`, `tex_pgf_driver.cpp` |
| `\fill` | Fill path | 🔶 Partial | `tikz.pkg.json`, `tex_pgf_driver.cpp` |
| `\filldraw` | Fill and draw path | 🔶 Partial | `tikz.pkg.json` |
| `\path` | Define path without drawing | 🔶 Partial | `tikz.pkg.json` |
| `\node` | Place a node | 🔶 Partial | `tikz.pkg.json`, `tex_pgf_driver.cpp` |
| `\coordinate` | Define coordinate | 🔶 Partial | `tikz.pkg.json` |
| `\clip` | Clip following content | 🔶 Partial | `tikz.pkg.json` |
| `\foreach` | Loop construct | 🔶 Partial | `tikz.pkg.json` |
| `tikzpicture` environment | TikZ drawing environment | 🔶 Partial | `tikz.pkg.json`, `tex_pgf_driver.cpp` |

---

## 10. Tables

### booktabs

**Package:** `booktabs.pkg.json`  
**Description:** Publication-quality tables with professional rules.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\toprule` | Top rule of table | ✅ Full | `booktabs.pkg.json` |
| `\midrule` | Middle rule of table | ✅ Full | `booktabs.pkg.json` |
| `\bottomrule` | Bottom rule of table | ✅ Full | `booktabs.pkg.json` |
| `\cmidrule` | Partial rule spanning columns | ✅ Full | `booktabs.pkg.json` |
| `\addlinespace` | Add extra space between rows | ✅ Full | `booktabs.pkg.json` |
| `\specialrule` | Rule with specified width/space | 🔶 Partial | `booktabs.pkg.json` |

### array

**Package:** `array.pkg.json`  
**Implementation:** `tex_align.cpp`  
**Description:** Extended array and tabular environments.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\newcolumntype` | Define a new column type | ✅ Full | `array.pkg.json` |
| `m{width}` column | Middle-aligned paragraph | ✅ Full | `array.pkg.json`, `tex_align.cpp` |
| `b{width}` column | Bottom-aligned paragraph | ✅ Full | `array.pkg.json`, `tex_align.cpp` |
| `>{decl}` prefix | Insert before column | ✅ Full | `array.pkg.json` |
| `<{decl}` suffix | Insert after column | ✅ Full | `array.pkg.json` |

### longtable

**Package:** `longtable.pkg.json`  
**Description:** Tables that span multiple pages.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `longtable` environment | Multi-page table | 🔶 Partial | `longtable.pkg.json` |
| `\endhead` | End of head on each page | 🔶 Partial | `longtable.pkg.json` |
| `\endfirsthead` | End of first page head | 🔶 Partial | `longtable.pkg.json` |
| `\endfoot` | End of foot on each page | 🔶 Partial | `longtable.pkg.json` |
| `\endlastfoot` | End of last page foot | 🔶 Partial | `longtable.pkg.json` |

### tabularx

**Package:** `tabularx.pkg.json`  
**Description:** Tables with auto-adjusting column widths.

| Feature | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `tabularx` environment | Auto-width table | ✅ Full | `tabularx.pkg.json` |
| `X` column type | Auto-expanding column | ✅ Full | `tabularx.pkg.json` |

---

## 11. Lists

### enumitem

**Package:** `enumitem.pkg.json`  
**Description:** Control layout of itemize, enumerate, description.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\setlist` | Set default list parameters | ✅ Full | `enumitem.pkg.json` |
| `\newlist` | Define a new list environment | 🔶 Partial | `enumitem.pkg.json` |
| `\renewlist` | Redefine an existing list | 🔶 Partial | `enumitem.pkg.json` |
| List options (label, leftmargin, etc.) | | ✅ Full | `enumitem.pkg.json` |

---

## 12. Code Listings

**Package:** `listings.pkg.json`  
**Description:** Typeset source code listings with syntax highlighting.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\lstset` | Set default listing options | ✅ Full | `listings.pkg.json` |
| `\lstinline` | Inline code | ✅ Full | `listings.pkg.json` |
| `\lstinputlisting` | Input listing from file | 🔶 Partial | `listings.pkg.json` |
| `\lstdefinestyle` | Define a named style | 🔶 Partial | `listings.pkg.json` |
| `\lstdefinelanguage` | Define a new language | 🔶 Partial | `listings.pkg.json` |
| `lstlisting` environment | Code listing environment | ✅ Full | `listings.pkg.json` |

---

## 13. Page Layout

### geometry

**Package:** `geometry.pkg.json`  
**Description:** Flexible interface to document dimensions.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\geometry` | Set page geometry options | ✅ Full | `geometry.pkg.json` |
| `\newgeometry` | Change geometry mid-document | 🔶 Partial | `geometry.pkg.json` |
| `\restoregeometry` | Restore original geometry | 🔶 Partial | `geometry.pkg.json` |
| `\savegeometry` | Save current geometry | ❌ Missing | — |

### fancyhdr

**Package:** `fancyhdr.pkg.json`  
**Description:** Extensive control of page headers and footers.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\pagestyle` | Set the page style | ✅ Full | `fancyhdr.pkg.json` |
| `\thispagestyle` | Set page style for current page | ✅ Full | `fancyhdr.pkg.json` |
| `\fancyhead` | Define header content | 🔶 Partial | `fancyhdr.pkg.json` |
| `\fancyfoot` | Define footer content | 🔶 Partial | `fancyhdr.pkg.json` |
| `\lhead`, `\chead`, `\rhead` | Header positions | 🔶 Partial | `fancyhdr.pkg.json` |
| `\lfoot`, `\cfoot`, `\rfoot` | Footer positions | 🔶 Partial | `fancyhdr.pkg.json` |

---

## 14. Floats

### float

**Package:** `float.pkg.json`  
**Description:** Improved interface for floating objects.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\floatstyle` | Set style for float definitions | 🔶 Partial | `float.pkg.json` |
| `\newfloat` | Define new float type | 🔶 Partial | `float.pkg.json` |
| `\floatname` | Set name for float type | 🔶 Partial | `float.pkg.json` |
| `H` placement | Exactly here (requires float) | ✅ Full | `float.pkg.json` |

### wrapfig

**Package:** `wrapfig.pkg.json`  
**Description:** Wrap text around figures.

| Environment | Description | Status | Implementation |
|-------------|-------------|--------|----------------|
| `wrapfigure` | Figure with text wrapping | 🔶 Partial | `wrapfig.pkg.json` |
| `wraptable` | Table with text wrapping | 🔶 Partial | `wrapfig.pkg.json` |

---

## 15. Math Extensions

### mathtools

**Package:** `mathtools.pkg.json`  
**Description:** Extensions and fixes for amsmath.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\DeclarePairedDelimiter` | Define paired delimiter | ✅ Full | `mathtools.pkg.json` |
| `\coloneqq`, `\eqqcolon` | Colon equals | ✅ Full | `mathtools.pkg.json` |
| `\prescript` | Prescripts | ✅ Full | `mathtools.pkg.json` |
| `\splitfrac` | Split fraction | ✅ Full | `mathtools.pkg.json` |
| `\cramped` | Cramped math style | 🔶 Partial | `mathtools.pkg.json` |
| `\smashoperator` | Smash limits | 🔶 Partial | `mathtools.pkg.json` |

### cancel

**Package:** `cancel.pkg.json`  
**Description:** Place lines through math formulae.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\cancel` | Diagonal line (NE) | ✅ Full | `cancel.pkg.json` |
| `\bcancel` | Diagonal line (SE) | ✅ Full | `cancel.pkg.json` |
| `\xcancel` | X through expression | ✅ Full | `cancel.pkg.json` |
| `\cancelto` | Cancel with value at end | ✅ Full | `cancel.pkg.json` |

---

## 16. References

### cleveref

**Package:** `cleveref.pkg.json`  
**Description:** Intelligent cross-referencing with auto-generated names.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\cref` | Clever reference (lowercase) | ✅ Full | `cleveref.pkg.json` |
| `\Cref` | Clever reference (capitalized) | ✅ Full | `cleveref.pkg.json` |
| `\crefrange` | Reference range | 🔶 Partial | `cleveref.pkg.json` |
| `\cpageref` | Page reference | 🔶 Partial | `cleveref.pkg.json` |
| `\namecref` | Name only | 🔶 Partial | `cleveref.pkg.json` |

### natbib

**Package:** `natbib.pkg.json`  
**Description:** Flexible bibliography support.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\citet` | Textual citation | ✅ Full | `natbib.pkg.json` |
| `\citep` | Parenthetical citation | ✅ Full | `natbib.pkg.json` |
| `\citealt`, `\citealp` | Alternate citations | ✅ Full | `natbib.pkg.json` |
| `\citeauthor`, `\citeyear` | Author/year only | ✅ Full | `natbib.pkg.json` |

---

## 17. Text Formatting

### ulem

**Package:** `ulem.pkg.json`  
**Description:** Underline and strikeout.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\uline` | Underline | ✅ Full | `ulem.pkg.json` |
| `\uuline` | Double underline | ✅ Full | `ulem.pkg.json` |
| `\uwave` | Wavy underline | ✅ Full | `ulem.pkg.json` |
| `\sout` | Strikeout | ✅ Full | `ulem.pkg.json` |
| `\xout` | Cross-hatch strikeout | ✅ Full | `ulem.pkg.json` |
| `\dashuline` | Dashed underline | ✅ Full | `ulem.pkg.json` |

### soul

**Package:** `soul.pkg.json`  
**Description:** Letterspacing, underlining, striking out.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\so` | Letterspacing | 🔶 Partial | `soul.pkg.json` |
| `\caps` | Small caps with spacing | 🔶 Partial | `soul.pkg.json` |
| `\ul` | Underline | ✅ Full | `soul.pkg.json` |
| `\st` | Strikethrough | ✅ Full | `soul.pkg.json` |
| `\hl` | Highlight | ✅ Full | `soul.pkg.json` |

---

## 18. Units & Numbers

### siunitx

**Package:** `siunitx.pkg.json`  
**Description:** Comprehensive SI units package.

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\SI` | Number with unit (v2) | ✅ Full | `siunitx.pkg.json` |
| `\si` | Unit only (v2) | ✅ Full | `siunitx.pkg.json` |
| `\num` | Format a number | ✅ Full | `siunitx.pkg.json` |
| `\qty` | Number with unit (v3) | ✅ Full | `siunitx.pkg.json` |
| `\unit` | Format a unit | ✅ Full | `siunitx.pkg.json` |
| `\ang` | Format an angle | ✅ Full | `siunitx.pkg.json` |
| `\numrange` | Range of numbers | ✅ Full | `siunitx.pkg.json` |
| Unit macros (`\meter`, `\kilogram`, etc.) | | ✅ Full | `siunitx.pkg.json` |

---

## 19. Other Packages

### textcomp

**Package:** `textcomp.pkg.json`

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\texteuro` | Euro symbol € | ✅ Full | `textcomp.pkg.json` |
| `\textdegree` | Degree symbol ° | ✅ Full | `textcomp.pkg.json` |
| `\textcelsius` | Celsius °C | ✅ Full | `textcomp.pkg.json` |
| `\textmu` | Micro symbol μ | ✅ Full | `textcomp.pkg.json` |
| `\texttimes` | Multiplication × | ✅ Full | `textcomp.pkg.json` |
| `\textdiv` | Division ÷ | ✅ Full | `textcomp.pkg.json` |
| `\textpm` | Plus-minus ± | ✅ Full | `textcomp.pkg.json` |

### multicol

**Package:** `multicol.pkg.json`

| Environment | Description | Status | Implementation |
|-------------|-------------|--------|----------------|
| `multicols` environment | Multiple column layout | ✅ Full | `multicol.pkg.json` |
| `\columnbreak` | Force column break | 🔶 Partial | `multicol.pkg.json` |

### caption / subcaption

**Packages:** `caption.pkg.json`, `subcaption.pkg.json`

| Command | Description | Status | Implementation |
|---------|-------------|--------|----------------|
| `\captionsetup` | Configure caption style | ✅ Full | `caption.pkg.json` |
| `\caption*` | Unnumbered caption | ✅ Full | `caption.pkg.json` |
| `\captionof` | Caption outside float | ✅ Full | `caption.pkg.json` |
| `subfigure` environment | Sub-figure | ✅ Full | `subcaption.pkg.json` |
| `subtable` environment | Sub-table | ✅ Full | `subcaption.pkg.json` |
| `\subcaption` | Sub-caption | ✅ Full | `subcaption.pkg.json` |

---

## Summary Statistics

| Category | Full | Partial | Missing |
|----------|------|---------|---------|
| TeX Base | 30 | 3 | 5 |
| LaTeX Base | 130+ | 5 | 5 |
| AMS Math | 180+ | 0 | 0 |
| AMS Symbols | 200+ | 0 | 0 |
| Graphics | 9 | 1 | 1 |
| Tables | 18 | 6 | 0 |
| Other Packages | 60+ | 15 | 5 |

**Overall Implementation Status:**
- Core text processing: ✅ Excellent
- Math typesetting: ✅ Excellent  
- Cross-references: ✅ Excellent
- Tables (basic): ✅ Excellent
- Graphics: ✅ Good
- Page layout: 🔶 Partial
- TikZ/PGF: 🔶 Basic only
- Advanced packages: 🔶 Varies

---

*Last updated: January 2025*  
*Source: Lambda TeX pipeline (`lambda/tex/packages/*.pkg.json`)*
