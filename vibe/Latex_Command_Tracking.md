# LaTeX Command Tracking

This document tracks all TeX/LaTeX commands currently implemented or being worked on in Lambda's LaTeX pipeline. Commands are grouped by package, starting from core TeX primitives to specialized packages.

**Status Legend:**
- ✅ **Full** - Fully implemented and tested
- 🔶 **Partial** - Basic implementation, some features missing
- ❌ **Missing** - Defined but not yet implemented

---

## 1. TeX Base (`tex_base.pkg.json`)

**Description:** Core TeX primitive commands that form the foundation of all typesetting operations.

### Grouping & Structure

| Command | Description | Status |
|---------|-------------|--------|
| `\relax` | Do nothing | ✅ Full |
| `\ignorespaces` | Ignore following spaces | ✅ Full |
| `\begingroup` | Begin a group | ✅ Full |
| `\endgroup` | End a group | ✅ Full |
| `\bgroup` | Begin group (alias) | ✅ Full |
| `\egroup` | End group (alias) | ✅ Full |

### Paragraph Control

| Command | Description | Status |
|---------|-------------|--------|
| `\par` | End paragraph | ✅ Full |
| `\indent` | Begin paragraph with indentation | ✅ Full |
| `\noindent` | Begin paragraph without indentation | ✅ Full |

### Spacing & Glue

| Command  | Description                   | Status     |
| -------- | ----------------------------- | ---------- |
| `\hskip` | Horizontal skip               | 🔶 Partial |
| `\vskip` | Vertical skip                 | 🔶 Partial |
| `\kern`  | Insert kern (fixed space)     | 🔶 Partial |
| `\hfil`  | Horizontal fill (order 1)     | 🔶 Partial |
| `\hfill` | Horizontal fill (order 2)     | 🔶 Partial |
| `\hss`   | Horizontal stretch and shrink | 🔶 Partial |
| `\vfil`  | Vertical fill (order 1)       | 🔶 Partial |
| `\vfill` | Vertical fill (order 2)       | 🔶 Partial |
| `\vss`   | Vertical stretch and shrink   | 🔶 Partial |

### Rules

| Command | Description | Status |
|---------|-------------|--------|
| `\hrule` | Horizontal rule | 🔶 Partial |
| `\vrule` | Vertical rule | 🔶 Partial |

### Penalties & Breaking

| Command | Description | Status |
|---------|-------------|--------|
| `\penalty` | Insert penalty | 🔶 Partial |
| `\break` | Force break (penalty -10000) | ✅ Full |
| `\nobreak` | Prevent break (penalty 10000) | ✅ Full |
| `\allowbreak` | Allow break (penalty 0) | ✅ Full |

### Boxes

| Command | Description | Status |
|---------|-------------|--------|
| `\hbox` | Horizontal box | 🔶 Partial |
| `\vbox` | Vertical box | 🔶 Partial |
| `\vtop` | Vertical box aligned at top | 🔶 Partial |
| `\raise` | Raise box | 🔶 Partial |
| `\lower` | Lower box | 🔶 Partial |
| `\moveleft` | Move box left | 🔶 Partial |
| `\moveright` | Move box right | 🔶 Partial |
| `\rlap` | Right overlap (zero-width) | 🔶 Partial |
| `\llap` | Left overlap (zero-width) | 🔶 Partial |

### Output & I/O

| Command | Description | Status |
|---------|-------------|--------|
| `\special` | Pass special command to output | 🔶 Partial |
| `\write` | Write to file | ❌ Missing |
| `\message` | Print message to terminal | ❌ Missing |
| `\mark` | Insert mark (for headers/footers) | ❌ Missing |
| `\insert` | Insert floating material | ❌ Missing |

### Characters

| Command | Description | Status |
|---------|-------------|--------|
| `\char` | Insert character by code | 🔶 Partial |
| `\accent` | Put accent over character | ✅ Full |

### Core TeX Commands Not Yet Implemented

| Command | Description | Status |
|---------|-------------|--------|
| `\def` | Define macro | ❌ Missing |
| `\let` | Assign meaning of one control sequence to another | ❌ Missing |
| `\gdef` | Global definition | ❌ Missing |
| `\edef` | Expanded definition | ❌ Missing |
| `\xdef` | Global expanded definition | ❌ Missing |
| `\futurelet` | Assign meaning of next token | ❌ Missing |
| `\aftergroup` | Execute after group closes | ❌ Missing |
| `\afterassignment` | Execute after assignment | ❌ Missing |
| `\expandafter` | Expand next token first | ❌ Missing |
| `\noexpand` | Prevent expansion | ❌ Missing |
| `\the` | Convert internal quantity to tokens | ❌ Missing |
| `\number` | Convert number to digits | ❌ Missing |
| `\romannumeral` | Convert to roman numerals | ❌ Missing |
| `\string` | Convert control sequence to string | ❌ Missing |
| `\csname`/`\endcsname` | Build control sequence from tokens | ❌ Missing |
| `\meaning` | Show meaning of token | ❌ Missing |
| `\if...` | Various conditionals | ❌ Missing |
| `\else` | Conditional else | ❌ Missing |
| `\fi` | End conditional | ❌ Missing |
| `\loop`/`\repeat` | Simple loop | ❌ Missing |
| `\input` | Include file | ❌ Missing |
| `\endinput` | End input from current file | ❌ Missing |
| `\openin`/`\closein`/`\read` | File input operations | ❌ Missing |
| `\openout`/`\closeout` | File output operations | ❌ Missing |
| `\halign`/`\valign` | Alignment primitives | ❌ Missing |
| `\cr` | End alignment row | ❌ Missing |
| `\span` | Span columns in alignment | ❌ Missing |
| `\omit` | Omit template in alignment | ❌ Missing |
| `\shipout` | Output page | ❌ Missing |
| `\hyphenation` | Define hyphenation exceptions | ❌ Missing |
| `\patterns` | Define hyphenation patterns | ❌ Missing |
| `\setbox` | Assign box register | ❌ Missing |
| `\box` | Use box register | ❌ Missing |
| `\copy` | Copy box register | ❌ Missing |
| `\unhbox`/`\unvbox` | Unpack box | ❌ Missing |
| `\wd`/`\ht`/`\dp` | Box dimensions | ❌ Missing |

---

## 2. LaTeX Base (`latex_base.pkg.json`)

**Description:** Standard LaTeX commands built on top of TeX primitives. Provides document structure, text formatting, and basic environments.

### Document Structure

| Command | Description | Status |
|---------|-------------|--------|
| `\documentclass` | Document class declaration | ✅ Full |
| `\usepackage` | Load package | ✅ Full |
| `\begin{document}` | Start document body | ✅ Full |
| `\end{document}` | End document body | ✅ Full |

### Sectioning

| Command | Description | Status |
|---------|-------------|--------|
| `\part` | Part heading | ✅ Full |
| `\part*` | Unnumbered part | ✅ Full |
| `\chapter` | Chapter heading | ✅ Full |
| `\chapter*` | Unnumbered chapter | ✅ Full |
| `\section` | Section heading | ✅ Full |
| `\section*` | Unnumbered section | ✅ Full |
| `\subsection` | Subsection heading | ✅ Full |
| `\subsection*` | Unnumbered subsection | ✅ Full |
| `\subsubsection` | Subsubsection heading | ✅ Full |
| `\subsubsection*` | Unnumbered subsubsection | ✅ Full |
| `\paragraph` | Paragraph heading | ✅ Full |
| `\subparagraph` | Subparagraph heading | ✅ Full |

### Text Formatting

| Command | Description | Status |
|---------|-------------|--------|
| `\textbf` | Bold text | ✅ Full |
| `\textit` | Italic text | ✅ Full |
| `\texttt` | Monospace text | ✅ Full |
| `\textrm` | Roman text | ✅ Full |
| `\textsf` | Sans-serif text | ✅ Full |
| `\textsc` | Small caps text | ✅ Full |
| `\textsl` | Slanted text | ✅ Full |
| `\textup` | Upright text | ✅ Full |
| `\textnormal` | Normal text | ✅ Full |
| `\emph` | Emphasized text | ✅ Full |
| `\underline` | Underlined text | ✅ Full |

### Font Switches

| Command | Description | Status |
|---------|-------------|--------|
| `\bfseries` | Switch to bold | ✅ Full |
| `\mdseries` | Switch to medium weight | ✅ Full |
| `\itshape` | Switch to italic | ✅ Full |
| `\upshape` | Switch to upright | ✅ Full |
| `\slshape` | Switch to slanted | ✅ Full |
| `\scshape` | Switch to small caps | ✅ Full |
| `\ttfamily` | Switch to monospace | ✅ Full |
| `\rmfamily` | Switch to roman | ✅ Full |
| `\sffamily` | Switch to sans-serif | ✅ Full |
| `\normalfont` | Switch to normal font | ✅ Full |

### Font Sizes

| Command | Description | Status |
|---------|-------------|--------|
| `\tiny` | Tiny font size (5pt) | ✅ Full |
| `\scriptsize` | Script font size (7pt) | ✅ Full |
| `\footnotesize` | Footnote size (8pt) | ✅ Full |
| `\small` | Small size (9pt) | ✅ Full |
| `\normalsize` | Normal size (10pt) | ✅ Full |
| `\large` | Large size (12pt) | ✅ Full |
| `\Large` | Larger size (14pt) | ✅ Full |
| `\LARGE` | Very large size (17pt) | ✅ Full |
| `\huge` | Huge size (20pt) | ✅ Full |
| `\Huge` | Very huge size (25pt) | ✅ Full |

### Cross-References

| Command | Description | Status |
|---------|-------------|--------|
| `\label` | Set label for cross-reference | ✅ Full |
| `\ref` | Reference to label | ✅ Full |
| `\pageref` | Page reference to label | 🔶 Partial |
| `\eqref` | Equation reference (with parentheses) | ✅ Full |

### Footnotes & Notes

| Command | Description | Status |
|---------|-------------|--------|
| `\footnote` | Footnote | ✅ Full |
| `\footnotemark` | Footnote mark only | 🔶 Partial |
| `\footnotetext` | Footnote text only | 🔶 Partial |

### Lists

| Command | Description | Status |
|---------|-------------|--------|
| `\item` | List item | ✅ Full |
| `\begin{itemize}` | Bullet list | ✅ Full |
| `\begin{enumerate}` | Numbered list | ✅ Full |
| `\begin{description}` | Description list | ✅ Full |

### Floats & Captions

| Command | Description | Status |
|---------|-------------|--------|
| `\caption` | Float caption | ✅ Full |
| `\begin{figure}` | Figure environment | ✅ Full |
| `\begin{figure*}` | Wide figure | ✅ Full |
| `\begin{table}` | Table environment | ✅ Full |
| `\begin{table*}` | Wide table | ✅ Full |

### Title & Abstract

| Command | Description | Status |
|---------|-------------|--------|
| `\title` | Document title | ✅ Full |
| `\author` | Document author | ✅ Full |
| `\date` | Document date | ✅ Full |
| `\thanks` | Author thanks | ✅ Full |
| `\maketitle` | Generate title block | ✅ Full |
| `\begin{abstract}` | Abstract environment | ✅ Full |

### Table of Contents

| Command | Description | Status |
|---------|-------------|--------|
| `\tableofcontents` | Table of contents | 🔶 Partial |
| `\listoffigures` | List of figures | 🔶 Partial |
| `\listoftables` | List of tables | 🔶 Partial |

### Spacing

| Command | Description | Status |
|---------|-------------|--------|
| `\hspace` | Horizontal space | ✅ Full |
| `\hspace*` | Horizontal space (preserved) | ✅ Full |
| `\vspace` | Vertical space | ✅ Full |
| `\vspace*` | Vertical space (preserved) | ✅ Full |
| `\quad` | Em space | ✅ Full |
| `\qquad` | Two em spaces | ✅ Full |
| `\enspace` | En space (0.5em) | ✅ Full |
| `\thinspace` | Thin space | ✅ Full |
| `\negthinspace` | Negative thin space | ✅ Full |
| `\,` | Thin space (math) | ✅ Full |
| `\:` | Medium space (math) | ✅ Full |
| `\;` | Thick space (math) | ✅ Full |
| `\!` | Negative thin space | ✅ Full |
| `\ ` | Control space | ✅ Full |
| `~` | Non-breaking space | ✅ Full |

### Page & Line Breaking

| Command | Description | Status |
|---------|-------------|--------|
| `\newline` | New line | ✅ Full |
| `\\` | Line break | ✅ Full |
| `\linebreak` | Line break with optional penalty | ✅ Full |
| `\nolinebreak` | Prevent line break | ✅ Full |
| `\pagebreak` | Page break | 🔶 Partial |
| `\nopagebreak` | Prevent page break | 🔶 Partial |
| `\newpage` | New page | ✅ Full |
| `\clearpage` | Clear page and flush floats | 🔶 Partial |
| `\cleardoublepage` | Clear to next odd page | 🔶 Partial |

### Alignment

| Command | Description | Status |
|---------|-------------|--------|
| `\centering` | Center text in environment | ✅ Full |
| `\raggedright` | Left-align text | ✅ Full |
| `\raggedleft` | Right-align text | ✅ Full |
| `\begin{center}` | Center environment | ✅ Full |
| `\begin{flushleft}` | Left-align environment | ✅ Full |
| `\begin{flushright}` | Right-align environment | ✅ Full |

### Special Characters

| Command | Description | Status |
|---------|-------------|--------|
| `\%` | Percent character | ✅ Full |
| `\&` | Ampersand character | ✅ Full |
| `\#` | Hash character | ✅ Full |
| `\$` | Dollar character | ✅ Full |
| `\_` | Underscore character | ✅ Full |
| `\{` | Left brace character | ✅ Full |
| `\}` | Right brace character | ✅ Full |
| `\textbackslash` | Backslash | ✅ Full |

### Logos & Symbols

| Command | Description | Status |
|---------|-------------|--------|
| `\TeX` | TeX logo | ✅ Full |
| `\LaTeX` | LaTeX logo | ✅ Full |
| `\LaTeXe` | LaTeX2ε logo | ✅ Full |
| `\today` | Current date | ✅ Full |
| `\ldots` | Ellipsis | ✅ Full |
| `\dots` | Ellipsis (alias) | ✅ Full |
| `\dag` | Dagger † | ✅ Full |
| `\ddag` | Double dagger ‡ | ✅ Full |
| `\S` | Section sign § | ✅ Full |
| `\P` | Pilcrow ¶ | ✅ Full |
| `\copyright` | Copyright © | ✅ Full |
| `\textregistered` | Registered ® | ✅ Full |
| `\texttrademark` | Trademark ™ | ✅ Full |
| `\pounds` | Pound sign £ | ✅ Full |

### Accents (Text Mode)

| Command | Description | Status |
|---------|-------------|--------|
| `` \` `` | Grave accent (è) | ✅ Full |
| `\'` | Acute accent (é) | ✅ Full |
| `\^` | Circumflex (ê) | ✅ Full |
| `\"` | Umlaut (ë) | ✅ Full |
| `\~` | Tilde (ñ) | ✅ Full |
| `\=` | Macron (ā) | ✅ Full |
| `\.` | Dot accent (ȧ) | ✅ Full |
| `\c` | Cedilla (ç) | ✅ Full |
| `\v` | Háček (č) | ✅ Full |
| `\u` | Breve (ă) | ✅ Full |
| `\H` | Hungarian umlaut (ő) | ✅ Full |
| `\r` | Ring (å) | ✅ Full |
| `\t` | Tie accent | ✅ Full |
| `\d` | Dot below (ạ) | ✅ Full |
| `\b` | Bar below | ✅ Full |

### Verbatim

| Command | Description | Status |
|---------|-------------|--------|
| `\verb` | Verbatim text | ✅ Full |
| `\verb*` | Verbatim with visible spaces | ✅ Full |
| `\begin{verbatim}` | Verbatim environment | ✅ Full |
| `\begin{verbatim*}` | Verbatim with visible spaces | ✅ Full |

### Macro Definition

| Command | Description | Status |
|---------|-------------|--------|
| `\newcommand` | Define new command | ✅ Full |
| `\renewcommand` | Redefine command | ✅ Full |
| `\providecommand` | Define if not exists | ✅ Full |
| `\newenvironment` | Define new environment | 🔶 Partial |
| `\renewenvironment` | Redefine environment | 🔶 Partial |

### Counters

| Command | Description | Status |
|---------|-------------|--------|
| `\newcounter` | Define new counter | ✅ Full |
| `\setcounter` | Set counter value | ✅ Full |
| `\addtocounter` | Add to counter | ✅ Full |
| `\stepcounter` | Increment counter | ✅ Full |
| `\refstepcounter` | Increment counter and set ref | ✅ Full |
| `\arabic` | Counter as arabic number | ✅ Full |
| `\roman` | Counter as lowercase roman | ✅ Full |
| `\Roman` | Counter as uppercase roman | ✅ Full |
| `\alph` | Counter as lowercase letter | ✅ Full |
| `\Alph` | Counter as uppercase letter | ✅ Full |
| `\fnsymbol` | Counter as footnote symbol | ✅ Full |

### Tables (Basic)

| Command | Description | Status |
|---------|-------------|--------|
| `\begin{tabular}` | Table environment | ✅ Full |
| `\begin{tabular*}` | Table with specified width | 🔶 Partial |
| `\hline` | Horizontal line | ✅ Full |
| `\cline` | Partial horizontal line | ✅ Full |
| `\multicolumn` | Span multiple columns | ✅ Full |
| `\begin{array}` | Math array | ✅ Full |

### Quotations

| Command | Description | Status |
|---------|-------------|--------|
| `\begin{quote}` | Short quotation | ✅ Full |
| `\begin{quotation}` | Long quotation | ✅ Full |
| `\begin{verse}` | Verse environment | ✅ Full |

### Math Environments (Basic)

| Command | Description | Status |
|---------|-------------|--------|
| `\begin{equation}` | Numbered equation | ✅ Full |
| `\begin{equation*}` | Unnumbered equation | ✅ Full |
| `\begin{displaymath}` | Display math | ✅ Full |
| `\begin{math}` | Inline math | ✅ Full |
| `$...$` | Inline math | ✅ Full |
| `$$...$$` | Display math | ✅ Full |
| `\[...\]` | Display math | ✅ Full |
| `\(...\)` | Inline math | ✅ Full |
| `\begin{eqnarray}` | Equation array | 🔶 Partial |
| `\begin{eqnarray*}` | Unnumbered equation array | 🔶 Partial |

### Boxes

| Command | Description | Status |
|---------|-------------|--------|
| `\begin{minipage}` | Minipage | 🔶 Partial |
| `\parbox` | Paragraph box | 🔶 Partial |
| `\mbox` | Horizontal box | ✅ Full |
| `\makebox` | Box with specified width | 🔶 Partial |
| `\fbox` | Framed box | 🔶 Partial |
| `\framebox` | Framed box with width | 🔶 Partial |
| `\raisebox` | Raised box | 🔶 Partial |

### Bibliography

| Command | Description | Status |
|---------|-------------|--------|
| `\begin{thebibliography}` | Bibliography environment | 🔶 Partial |
| `\bibitem` | Bibliography item | 🔶 Partial |
| `\cite` | Citation | ✅ Full |

### Core LaTeX Commands Not Yet Implemented

| Command | Description | Status |
|---------|-------------|--------|
| `\include` | Include file (starts new page) | ❌ Missing |
| `\includeonly` | Specify files to include | ❌ Missing |
| `\input` | Include file without page break | ❌ Missing |
| `\marginpar` | Marginal note | ❌ Missing |
| `\rule` | Produce rule box | ❌ Missing |
| `\savebox` | Save box for later use | ❌ Missing |
| `\usebox` | Use saved box | ❌ Missing |
| `\newsavebox` | Declare box register | ❌ Missing |
| `\sloppy` | Use loose line breaking | ❌ Missing |
| `\fussy` | Use strict line breaking | ❌ Missing |
| `\hyphenation` | Define hyphenation exceptions | ❌ Missing |
| `\index` | Generate index entry | ❌ Missing |
| `\printindex` | Print index | ❌ Missing |
| `\glossary` | Glossary entry | ❌ Missing |
| `\addcontentsline` | Add to table of contents | ❌ Missing |
| `\addtocontents` | Write to toc file | ❌ Missing |

---

## 3. AMS Math (`amsmath.pkg.json`)

**Description:** American Mathematical Society extensions. Essential package for professional mathematical typesetting. Provides advanced math constructs, alignment environments, and operators.

### Fractions

| Command | Description | Status |
|---------|-------------|--------|
| `\frac` | Fraction | ✅ Full |
| `\dfrac` | Display-style fraction | ✅ Full |
| `\tfrac` | Text-style fraction | ✅ Full |
| `\cfrac` | Continued fraction | ✅ Full |
| `\genfrac` | Generalized fraction | 🔶 Partial |

### Binomials

| Command | Description | Status |
|---------|-------------|--------|
| `\binom` | Binomial coefficient | ✅ Full |
| `\dbinom` | Display-style binomial | ✅ Full |
| `\tbinom` | Text-style binomial | ✅ Full |

### Roots & Radicals

| Command | Description | Status |
|---------|-------------|--------|
| `\sqrt` | Square/nth root | ✅ Full |

### Text in Math

| Command | Description | Status |
|---------|-------------|--------|
| `\text` | Text in math | ✅ Full |
| `\intertext` | Text between align rows | ✅ Full |
| `\shortintertext` | Short intertext | ✅ Full |

### Operators

| Command | Description | Status |
|---------|-------------|--------|
| `\operatorname` | Named operator | ✅ Full |
| `\operatorname*` | Named operator with limits | ✅ Full |
| `\DeclareMathOperator` | Declare math operator | ✅ Full |
| `\DeclareMathOperator*` | Declare with limits | ✅ Full |

### Decorations

| Command | Description | Status |
|---------|-------------|--------|
| `\boldsymbol` | Bold symbol | ✅ Full |
| `\pmb` | Poor man's bold | ✅ Full |
| `\overset` | Symbol with accent above | ✅ Full |
| `\underset` | Symbol with accent below | ✅ Full |
| `\stackrel` | Symbol with accent (deprecated) | ✅ Full |
| `\sideset` | Side scripts on large operator | 🔶 Partial |
| `\substack` | Stacked subscripts/superscripts | ✅ Full |

### Extensible Arrows

| Command | Description | Status |
|---------|-------------|--------|
| `\xleftarrow` | Left arrow with text | ✅ Full |
| `\xrightarrow` | Right arrow with text | ✅ Full |

### Tags & Numbering

| Command | Description | Status |
|---------|-------------|--------|
| `\tag` | Custom equation tag | ✅ Full |
| `\tag*` | Custom tag (no parens) | ✅ Full |
| `\notag` | Suppress equation number | ✅ Full |
| `\nonumber` | Suppress equation number (alias) | ✅ Full |
| `\numberwithin` | Reset counter within | 🔶 Partial |

### Boxing

| Command | Description | Status |
|---------|-------------|--------|
| `\boxed` | Boxed equation | ✅ Full |

### Modular Arithmetic

| Command | Description | Status |
|---------|-------------|--------|
| `\mod` | Modulo | ✅ Full |
| `\bmod` | Binary modulo | ✅ Full |
| `\pmod` | Parenthesized modulo | ✅ Full |
| `\pod` | Parenthesized | ✅ Full |

### Multi-line Environments

| Environment | Description | Status |
|-------------|-------------|--------|
| `align` | Aligned equations (numbered) | ✅ Full |
| `align*` | Aligned equations (unnumbered) | ✅ Full |
| `alignat` | Aligned with column count | ✅ Full |
| `alignat*` | Alignat unnumbered | ✅ Full |
| `aligned` | Aligned subequations | ✅ Full |
| `alignedat` | Alignedat with columns | ✅ Full |
| `gather` | Gathered equations (numbered) | ✅ Full |
| `gather*` | Gathered equations (unnumbered) | ✅ Full |
| `gathered` | Gathered subequations | ✅ Full |
| `multline` | Multi-line equation (numbered) | ✅ Full |
| `multline*` | Multi-line equation (unnumbered) | ✅ Full |
| `flalign` | Full-width aligned | 🔶 Partial |
| `flalign*` | Full-width aligned (unnumbered) | 🔶 Partial |
| `split` | Split equation | ✅ Full |

### Cases

| Environment | Description | Status |
|-------------|-------------|--------|
| `cases` | Cases | ✅ Full |
| `dcases` | Display-style cases | ✅ Full |
| `rcases` | Right-side cases | ✅ Full |
| `drcases` | Display right-side cases | ✅ Full |

### Matrices

| Environment | Description | Status |
|-------------|-------------|--------|
| `matrix` | Plain matrix | ✅ Full |
| `pmatrix` | Matrix with parentheses | ✅ Full |
| `bmatrix` | Matrix with brackets | ✅ Full |
| `Bmatrix` | Matrix with braces | ✅ Full |
| `vmatrix` | Matrix with vertical bars | ✅ Full |
| `Vmatrix` | Matrix with double bars | ✅ Full |
| `smallmatrix` | Small inline matrix | ✅ Full |
| `subarray` | Stacked subscript array | ✅ Full |

### Math Functions

| Command | Description | Status |
|---------|-------------|--------|
| `\sin`, `\cos`, `\tan` | Trigonometric | ✅ Full |
| `\sec`, `\csc`, `\cot` | Trigonometric | ✅ Full |
| `\arcsin`, `\arccos`, `\arctan` | Inverse trig | ✅ Full |
| `\sinh`, `\cosh`, `\tanh`, `\coth` | Hyperbolic | ✅ Full |
| `\log`, `\ln`, `\lg`, `\exp` | Logarithmic | ✅ Full |
| `\lim`, `\limsup`, `\liminf` | Limits | ✅ Full |
| `\max`, `\min`, `\sup`, `\inf` | Extrema | ✅ Full |
| `\arg`, `\det`, `\dim`, `\hom`, `\ker` | Various | ✅ Full |
| `\deg`, `\gcd`, `\Pr` | Various | ✅ Full |

### Integrals

| Command | Description | Status |
|---------|-------------|--------|
| `\int` | Integral | ✅ Full |
| `\iint` | Double integral | ✅ Full |
| `\iiint` | Triple integral | ✅ Full |
| `\iiiint` | Quadruple integral | ✅ Full |
| `\idotsint` | Multiple integral with dots | ✅ Full |
| `\oint` | Contour integral | ✅ Full |
| `\oiint` | Surface integral | ✅ Full |
| `\oiiint` | Volume integral | ✅ Full |

### Big Operators

| Command | Description | Status |
|---------|-------------|--------|
| `\sum`, `\prod`, `\coprod` | Sum/Product | ✅ Full |
| `\bigcup`, `\bigcap` | Set operations | ✅ Full |
| `\bigsqcup` | Square union | ✅ Full |
| `\bigvee`, `\bigwedge` | Logical | ✅ Full |
| `\bigodot`, `\bigoplus`, `\bigotimes` | Circled | ✅ Full |
| `\biguplus` | Multiset union | ✅ Full |

### Delimiters

| Command | Description | Status |
|---------|-------------|--------|
| `\left`, `\right` | Auto-sizing delimiters | ✅ Full |
| `\middle` | Auto-sizing middle delimiter | ✅ Full |
| `\big`, `\Big`, `\bigg`, `\Bigg` | Manual sizing | ✅ Full |
| `\bigl`, `\Bigl`, `\biggl`, `\Biggl` | Left delimiters | ✅ Full |
| `\bigr`, `\Bigr`, `\biggr`, `\Biggr` | Right delimiters | ✅ Full |
| `\bigm`, `\Bigm`, `\biggm`, `\Biggm` | Middle delimiters | ✅ Full |

### Greek Letters

| Commands | Status |
|----------|--------|
| `\alpha` through `\omega` (lowercase) | ✅ Full |
| `\Gamma` through `\Omega` (uppercase) | ✅ Full |
| Variant forms (`\varepsilon`, `\vartheta`, etc.) | ✅ Full |

### Arrows

| Command | Description | Status |
|---------|-------------|--------|
| `\to`, `\gets` | Basic arrows | ✅ Full |
| `\rightarrow`, `\leftarrow` | Arrows | ✅ Full |
| `\Rightarrow`, `\Leftarrow` | Double arrows | ✅ Full |
| `\leftrightarrow`, `\Leftrightarrow` | Double-headed | ✅ Full |
| `\uparrow`, `\downarrow` | Vertical arrows | ✅ Full |
| `\mapsto`, `\longmapsto` | Maps to | ✅ Full |
| `\implies`, `\impliedby`, `\iff` | Logical | ✅ Full |
| Long arrow variants | ✅ Full |
| Hook and harpoon arrows | ✅ Full |

### Relations

| Command | Description | Status |
|---------|-------------|--------|
| `\leq`, `\geq`, `\neq` | Comparisons | ✅ Full |
| `\ll`, `\gg` | Much less/greater | ✅ Full |
| `\sim`, `\simeq`, `\cong`, `\approx` | Similarities | ✅ Full |
| `\equiv`, `\propto` | Equivalence | ✅ Full |
| `\prec`, `\succ`, `\preceq`, `\succeq` | Precedence | ✅ Full |
| `\subset`, `\supset`, `\subseteq`, `\supseteq` | Subsets | ✅ Full |
| `\in`, `\ni`, `\notin` | Membership | ✅ Full |
| `\mid`, `\parallel`, `\perp` | Geometric | ✅ Full |
| `\vdash`, `\dashv`, `\models` | Turnstiles | ✅ Full |

### Binary Operations

| Command | Description | Status |
|---------|-------------|--------|
| `\pm`, `\mp` | Plus/minus | ✅ Full |
| `\times`, `\div`, `\cdot` | Multiplication/division | ✅ Full |
| `\oplus`, `\ominus`, `\otimes`, `\oslash`, `\odot` | Circled | ✅ Full |
| `\wedge`, `\vee` (or `\land`, `\lor`) | Logical | ✅ Full |
| `\cap`, `\cup`, `\sqcap`, `\sqcup` | Set operations | ✅ Full |
| `\setminus` | Set difference | ✅ Full |

### Miscellaneous Symbols

| Command | Description | Status |
|---------|-------------|--------|
| `\forall`, `\exists`, `\nexists` | Quantifiers | ✅ Full |
| `\neg` / `\lnot` | Negation | ✅ Full |
| `\emptyset`, `\varnothing` | Empty set | ✅ Full |
| `\infty`, `\nabla`, `\partial` | Calculus | ✅ Full |
| `\aleph`, `\beth`, `\hbar`, `\ell` | Special | ✅ Full |
| `\wp`, `\Re`, `\Im` | Special functions | ✅ Full |
| `\angle`, `\triangle`, `\square`, `\diamond` | Shapes | ✅ Full |
| `\prime`, `\backprime` | Primes | ✅ Full |
| `\cdots`, `\ddots`, `\vdots`, `\ldots` | Dots | ✅ Full |

### Bracket Delimiters

| Command | Description | Status |
|---------|-------------|--------|
| `\lvert`, `\rvert` | Vertical bars | ✅ Full |
| `\lVert`, `\rVert` | Double vertical bars | ✅ Full |
| `\langle`, `\rangle` | Angle brackets | ✅ Full |
| `\lceil`, `\rceil` | Ceiling | ✅ Full |
| `\lfloor`, `\rfloor` | Floor | ✅ Full |
| `\lbrace`, `\rbrace` | Braces | ✅ Full |
| `\lbrack`, `\rbrack` | Brackets | ✅ Full |

---

## 4. AMS Symbols (`amssymb.pkg.json`)

**Description:** Extended symbol collection from AMS. Provides additional mathematical symbols not in base TeX.

### Extended Relations

| Commands | Status |
|----------|--------|
| `\leqq`, `\geqq`, `\lneqq`, `\gneqq` | ✅ Full |
| `\lesssim`, `\gtrsim`, `\lessapprox`, `\gtrapprox` | ✅ Full |
| `\lessgtr`, `\gtrless`, `\lesseqgtr`, `\gtreqless` | ✅ Full |
| `\lll`, `\ggg` (triple less/greater) | ✅ Full |
| `\doteq`, `\triangleq`, `\bumpeq` | ✅ Full |
| `\preccurlyeq`, `\succcurlyeq`, `\precsim`, `\succsim` | ✅ Full |
| `\Subset`, `\Supset`, `\subseteqq`, `\supseteqq` | ✅ Full |
| Negated relations (`\nless`, `\ngtr`, `\nleq`, etc.) | ✅ Full |
| Triangle relations | ✅ Full |

### Extended Binary Operations

| Commands | Status |
|----------|--------|
| `\divideontimes`, `\dotplus`, `\smallsetminus` | ✅ Full |
| `\Cap`, `\Cup`, `\barwedge`, `\veebar` | ✅ Full |
| `\curlywedge`, `\curlyvee` | ✅ Full |
| `\ltimes`, `\rtimes`, `\leftthreetimes`, `\rightthreetimes` | ✅ Full |
| `\circledast`, `\circledcirc`, `\circleddash` | ✅ Full |
| `\boxplus`, `\boxminus`, `\boxtimes`, `\boxdot` | ✅ Full |

### Extended Arrows

| Commands | Status |
|----------|--------|
| `\twoheadleftarrow`, `\twoheadrightarrow` | ✅ Full |
| `\leftleftarrows`, `\rightrightarrows` | ✅ Full |
| `\leftrightarrows`, `\rightleftarrows` | ✅ Full |
| Harpoon arrows | ✅ Full |
| Negated arrows | ✅ Full |
| `\dashrightarrow`, `\dashleftarrow`, `\leadsto` | ✅ Full |

### Miscellaneous Symbols

| Commands | Status |
|----------|--------|
| `\therefore`, `\because` | ✅ Full |
| `\complement`, `\mho`, `\eth` | ✅ Full |
| `\Finv`, `\Game`, `\gimel`, `\daleth` | ✅ Full |
| `\digamma`, `\varkappa` | ✅ Full |
| `\circledS`, `\circledR`, `\Bbbk` | ✅ Full |
| `\hslash` | ✅ Full |
| `\lozenge`, `\blacklozenge`, `\bigstar` | ✅ Full |
| `\blacksquare`, `\square` | ✅ Full |
| Corner brackets (`\ulcorner`, etc.) | ✅ Full |
| `\diagup`, `\diagdown` | ✅ Full |
| Musical symbols (`\flat`, `\natural`, `\sharp`) | ✅ Full |
| Card suit symbols | ✅ Full |

### Font Commands

| Command | Description | Status |
|---------|-------------|--------|
| `\mathbb` | Blackboard bold | ✅ Full |
| `\mathfrak` | Fraktur font | ✅ Full |
| `\mathscr` | Script font | ✅ Full |

---

## 5. AMS Theorem (`amsthm.pkg.json`)

**Description:** Theorem-like environments with customizable styles.

| Command/Environment | Description | Status |
|---------------------|-------------|--------|
| `\newtheorem` | Define theorem-like environment | 🔶 Partial |
| `\theoremstyle` | Set style for theorem definitions | 🔶 Partial |
| `\newtheoremstyle` | Define custom theorem style | ❌ Missing |
| `\swapnumbers` | Put numbers before theorem name | ❌ Missing |
| `\qed` | End of proof mark | ✅ Full |
| `\qedhere` | Place QED at current location | ✅ Full |
| `proof` environment | Proof environment | ✅ Full |

---

## 6. Graphics (`graphicx.pkg.json`)

**Description:** Standard package for including images and performing graphical transformations.

### Image Inclusion

| Command | Description | Status |
|---------|-------------|--------|
| `\includegraphics` | Include an image | ✅ Full |
| `\graphicspath` | Set image search paths | 🔶 Partial |
| `\DeclareGraphicsExtensions` | Set file extensions | ❌ Missing |
| `\DeclareGraphicsRule` | Define graphics handling | ❌ Missing |

### Transformations

| Command | Description | Status |
|---------|-------------|--------|
| `\rotatebox` | Rotate content | 🔶 Partial |
| `\scalebox` | Scale content | 🔶 Partial |
| `\reflectbox` | Horizontally reflect content | 🔶 Partial |
| `\resizebox` | Resize to specific dimensions | 🔶 Partial |
| `\resizebox*` | Resize (total height) | 🔶 Partial |

### Options Supported

| Option | Description | Status |
|--------|-------------|--------|
| `width` | Width to scale to | ✅ Full |
| `height` | Height to scale to | ✅ Full |
| `scale` | Scale factor | ✅ Full |
| `keepaspectratio` | Maintain aspect ratio | ✅ Full |
| `angle` | Rotation angle | 🔶 Partial |
| `clip`, `trim`, `viewport` | Cropping | 🔶 Partial |
| `page` | PDF page number | 🔶 Partial |

---

## 7. Hyperref (`hyperref.pkg.json`)

**Description:** Hypertext links and PDF metadata. Creates clickable links and cross-references.

### Links

| Command | Description | Status |
|---------|-------------|--------|
| `\href` | Hyperlink with text | ✅ Full |
| `\url` | URL in typewriter font | ✅ Full |
| `\nolinkurl` | URL without hyperlink | ✅ Full |
| `\hyperref` | Internal reference with custom text | 🔶 Partial |
| `\hyperlink` | Create link to anchor | 🔶 Partial |
| `\hypertarget` | Create anchor | 🔶 Partial |

### References

| Command | Description | Status |
|---------|-------------|--------|
| `\autoref` | Reference with auto-generated name | 🔶 Partial |
| `\autopageref` | Page reference with auto name | ❌ Missing |
| `\nameref` | Reference by section name | 🔶 Partial |

### PDF Features

| Command | Description | Status |
|---------|-------------|--------|
| `\hypersetup` | Configure hyperref options | 🔶 Partial |
| `\phantomsection` | Invisible anchor | ✅ Full |
| `\bookmark` | Add PDF bookmark | ❌ Missing |
| `\pdfbookmark` | Add PDF bookmark with level | ❌ Missing |
| `\texorpdfstring` | Different text for TeX/PDF | ✅ Full |

---

## 8. Color Packages

### xcolor (`xcolor.pkg.json`)

**Description:** Extended color support with multiple color models.

| Command | Description | Status |
|---------|-------------|--------|
| `\color` | Switch to specified color | ✅ Full |
| `\textcolor` | Typeset text in color | ✅ Full |
| `\colorbox` | Box with colored background | ✅ Full |
| `\fcolorbox` | Box with colored frame and background | 🔶 Partial |
| `\pagecolor` | Set page background color | ❌ Missing |
| `\definecolor` | Define a new color | 🔶 Partial |
| `\colorlet` | Define color as copy | 🔶 Partial |
| `\rowcolors` | Alternate row colors in tables | ❌ Missing |

### color (`color.pkg.json`)

Basic color package (subset of xcolor). Most commands same as xcolor.

---

## 9. TikZ/PGF (`tikz.pkg.json`)

**Description:** Create graphics programmatically. Very extensive vector graphics system.

### Core Commands

| Command | Description | Status |
|---------|-------------|--------|
| `\tikz` | Inline TikZ drawing | 🔶 Partial |
| `\tikzset` | Set TikZ options globally | 🔶 Partial |
| `\usetikzlibrary` | Load TikZ libraries | 🔶 Partial |
| `\draw` | Draw path | 🔶 Partial |
| `\fill` | Fill path | 🔶 Partial |
| `\filldraw` | Fill and draw path | 🔶 Partial |
| `\path` | Define path without drawing | 🔶 Partial |
| `\node` | Place a node | 🔶 Partial |
| `\coordinate` | Define coordinate | 🔶 Partial |
| `\clip` | Clip following content | 🔶 Partial |
| `\foreach` | Loop construct | 🔶 Partial |

### PGF Math

| Command | Description | Status |
|---------|-------------|--------|
| `\pgfmathsetmacro` | Define macro with math result | 🔶 Partial |
| `\pgfmathparse` | Parse math expression | 🔶 Partial |

### Environments

| Environment | Description | Status |
|-------------|-------------|--------|
| `tikzpicture` | TikZ drawing environment | 🔶 Partial |
| `scope` | Scope for local settings | 🔶 Partial |
| `pgfonlayer` | Layer environment | ❌ Missing |

**Note:** TikZ is extremely complex. Full support requires significant additional work.

---

## 10. Tables

### booktabs (`booktabs.pkg.json`)

**Description:** Publication-quality tables with professional rules.

| Command | Description | Status |
|---------|-------------|--------|
| `\toprule` | Top rule of table | ✅ Full |
| `\midrule` | Middle rule of table | ✅ Full |
| `\bottomrule` | Bottom rule of table | ✅ Full |
| `\cmidrule` | Partial rule spanning columns | 🔶 Partial |
| `\addlinespace` | Add extra space between rows | 🔶 Partial |
| `\specialrule` | Rule with specified width/space | 🔶 Partial |

### array (`array.pkg.json`)

**Description:** Extended array and tabular environments.

| Command | Description | Status |
|---------|-------------|--------|
| `\newcolumntype` | Define a new column type | 🔶 Partial |
| `\firsthline` | First horizontal line | 🔶 Partial |
| `\lasthline` | Last horizontal line | 🔶 Partial |
| `m{width}` column | Middle-aligned paragraph | ✅ Full |
| `b{width}` column | Bottom-aligned paragraph | ✅ Full |
| `>{decl}` prefix | Insert before column | 🔶 Partial |
| `<{decl}` suffix | Insert after column | 🔶 Partial |

### longtable (`longtable.pkg.json`)

**Description:** Tables that span multiple pages.

| Command | Description | Status |
|---------|-------------|--------|
| `longtable` environment | Multi-page table | 🔶 Partial |
| `\endhead` | End of head on each page | 🔶 Partial |
| `\endfirsthead` | End of first page head | 🔶 Partial |
| `\endfoot` | End of foot on each page | 🔶 Partial |
| `\endlastfoot` | End of last page foot | 🔶 Partial |

### tabularx (`tabularx.pkg.json`)

**Description:** Tables with auto-adjusting column widths.

| Feature | Description | Status |
|---------|-------------|--------|
| `tabularx` environment | Auto-width table | 🔶 Partial |
| `X` column type | Auto-expanding column | 🔶 Partial |

---

## 11. Lists

### enumitem (`enumitem.pkg.json`)

**Description:** Control layout of itemize, enumerate, description.

| Command | Description | Status |
|---------|-------------|--------|
| `\setlist` | Set default list parameters | 🔶 Partial |
| `\newlist` | Define a new list environment | ❌ Missing |
| `\renewlist` | Redefine an existing list | ❌ Missing |
| Inline list environments | `itemize*`, `enumerate*` | ❌ Missing |
| List options (label, leftmargin, etc.) | 🔶 Partial |

---

## 12. Code Listings (`listings.pkg.json`)

**Description:** Typeset source code listings with syntax highlighting.

| Command | Description | Status |
|---------|-------------|--------|
| `\lstset` | Set default listing options | 🔶 Partial |
| `\lstinline` | Inline code | ✅ Full |
| `\lstinputlisting` | Input listing from file | 🔶 Partial |
| `\lstdefinestyle` | Define a named style | ❌ Missing |
| `\lstdefinelanguage` | Define a new language | ❌ Missing |
| `lstlisting` environment | Code listing environment | 🔶 Partial |

---

## 13. Page Layout

### geometry (`geometry.pkg.json`)

**Description:** Flexible interface to document dimensions.

| Command | Description | Status |
|---------|-------------|--------|
| `\geometry` | Set page geometry options | 🔶 Partial |
| `\newgeometry` | Change geometry mid-document | ❌ Missing |
| `\restoregeometry` | Restore original geometry | ❌ Missing |
| `\savegeometry` | Save current geometry | ❌ Missing |

### fancyhdr (`fancyhdr.pkg.json`)

**Description:** Extensive control of page headers and footers.

| Command | Description | Status |
|---------|-------------|--------|
| `\pagestyle` | Set the page style | 🔶 Partial |
| `\thispagestyle` | Set page style for current page | 🔶 Partial |
| `\fancyhead` | Define header content | ❌ Missing |
| `\fancyfoot` | Define footer content | ❌ Missing |
| `\lhead`, `\chead`, `\rhead` | Header positions | ❌ Missing |
| `\lfoot`, `\cfoot`, `\rfoot` | Footer positions | ❌ Missing |

---

## 14. Floats

### float (`float.pkg.json`)

**Description:** Improved interface for floating objects.

| Command | Description | Status |
|---------|-------------|--------|
| `\floatstyle` | Set style for float definitions | ❌ Missing |
| `\newfloat` | Define new float type | ❌ Missing |
| `\floatname` | Set name for float type | ❌ Missing |
| `H` placement | Exactly here (requires float) | 🔶 Partial |

### wrapfig (`wrapfig.pkg.json`)

**Description:** Wrap text around figures.

| Environment | Description | Status |
|-------------|-------------|--------|
| `wrapfigure` | Figure with text wrapping | 🔶 Partial |
| `wraptable` | Table with text wrapping | 🔶 Partial |

---

## 15. Math Extensions

### mathtools (`mathtools.pkg.json`)

**Description:** Extensions and fixes for amsmath.

| Command | Description | Status |
|---------|-------------|--------|
| `\DeclarePairedDelimiter` | Define paired delimiter | 🔶 Partial |
| `\coloneqq`, `\eqqcolon` | Colon equals | ✅ Full |
| `\prescript` | Prescripts | 🔶 Partial |
| `\splitfrac` | Split fraction | 🔶 Partial |
| `\cramped` | Cramped math style | ❌ Missing |
| `\smashoperator` | Smash limits | ❌ Missing |

### cancel (`cancel.pkg.json`)

**Description:** Place lines through math formulae.

| Command | Description | Status |
|---------|-------------|--------|
| `\cancel` | Diagonal line (NE) | ✅ Full |
| `\bcancel` | Diagonal line (SE) | ✅ Full |
| `\xcancel` | X through expression | ✅ Full |
| `\cancelto` | Cancel with value at end | ✅ Full |

### accents (`accents.pkg.json`)

**Description:** Multiple mathematical accents.

| Command | Description | Status |
|---------|-------------|--------|
| `\accentset` | Place accent on symbol | 🔶 Partial |
| `\underaccent` | Place accent below | 🔶 Partial |
| `\undertilde` | Tilde below | 🔶 Partial |
| `\dddot`, `\ddddot` | Triple/quadruple dot | ✅ Full |

---

## 16. References

### cleveref (`cleveref.pkg.json`)

**Description:** Intelligent cross-referencing with auto-generated names.

| Command | Description | Status |
|---------|-------------|--------|
| `\cref` | Clever reference (lowercase) | 🔶 Partial |
| `\Cref` | Clever reference (capitalized) | 🔶 Partial |
| `\crefrange` | Reference range | ❌ Missing |
| `\cpageref` | Page reference | ❌ Missing |
| `\namecref` | Name only | ❌ Missing |

### natbib (`natbib.pkg.json`)

**Description:** Flexible bibliography support.

| Command | Description | Status |
|---------|-------------|--------|
| `\citet` | Textual citation | 🔶 Partial |
| `\citep` | Parenthetical citation | 🔶 Partial |
| `\citealt`, `\citealp` | Alternate citations | 🔶 Partial |
| `\citeauthor`, `\citeyear` | Author/year only | 🔶 Partial |

---

## 17. Text Formatting

### ulem (`ulem.pkg.json`)

**Description:** Underline and strikeout.

| Command | Description | Status |
|---------|-------------|--------|
| `\uline` | Underline | ✅ Full |
| `\uuline` | Double underline | 🔶 Partial |
| `\uwave` | Wavy underline | 🔶 Partial |
| `\sout` | Strikeout | ✅ Full |
| `\xout` | Cross-hatch strikeout | 🔶 Partial |
| `\dashuline` | Dashed underline | 🔶 Partial |

### soul (`soul.pkg.json`)

**Description:** Letterspacing, underlining, striking out.

| Command | Description | Status |
|---------|-------------|--------|
| `\so` | Letterspacing | 🔶 Partial |
| `\caps` | Small caps with spacing | 🔶 Partial |
| `\ul` | Underline | 🔶 Partial |
| `\st` | Strikethrough | 🔶 Partial |
| `\hl` | Highlight | 🔶 Partial |

---

## 18. Units & Numbers

### siunitx (`siunitx.pkg.json`)

**Description:** Comprehensive SI units package.

| Command | Description | Status |
|---------|-------------|--------|
| `\SI` | Number with unit (v2) | 🔶 Partial |
| `\si` | Unit only (v2) | 🔶 Partial |
| `\num` | Format a number | 🔶 Partial |
| `\qty` | Number with unit (v3) | 🔶 Partial |
| `\unit` | Format a unit | 🔶 Partial |
| `\ang` | Format an angle | 🔶 Partial |
| `\numrange` | Range of numbers | 🔶 Partial |
| Unit macros (`\meter`, `\kilogram`, etc.) | 🔶 Partial |

---

## 19. Multilingual

### babel (`babel.pkg.json`)

**Description:** Multilingual support for LaTeX.

| Command | Description | Status |
|---------|-------------|--------|
| `\selectlanguage` | Switch to specified language | ❌ Missing |
| `\foreignlanguage` | Typeset text in foreign language | 🔶 Partial |
| `\shorthandoff` | Disable shorthand characters | ❌ Missing |
| `otherlanguage` environment | Environment for different language | ❌ Missing |

---

## 20. Other Packages

### verbatim (`verbatim.pkg.json`)

| Environment | Description | Status |
|-------------|-------------|--------|
| `verbatim` environment | Enhanced verbatim | ✅ Full |
| `comment` environment | Comment out text | ❌ Missing |
| `\verbatiminput` | Input file verbatim | ❌ Missing |

### fancyvrb (`fancyvrb.pkg.json`)

| Environment | Description | Status |
|-------------|-------------|--------|
| `Verbatim` environment | Enhanced verbatim with options | 🔶 Partial |
| `\VerbatimInput` | Input file with options | ❌ Missing |

### multicol (`multicol.pkg.json`)

| Environment | Description | Status |
|-------------|-------------|--------|
| `multicols` environment | Multiple column layout | 🔶 Partial |
| `\columnbreak` | Force column break | ❌ Missing |

### caption (`caption.pkg.json`)

| Command | Description | Status |
|---------|-------------|--------|
| `\captionsetup` | Configure caption style | 🔶 Partial |
| `\caption*` | Unnumbered caption | 🔶 Partial |
| `\captionof` | Caption outside float | 🔶 Partial |

### subcaption (`subcaption.pkg.json`)

| Environment | Description | Status |
|-------------|-------------|--------|
| `subfigure` environment | Sub-figure | 🔶 Partial |
| `subtable` environment | Sub-table | 🔶 Partial |
| `\subcaption` | Sub-caption | 🔶 Partial |

### inputenc (`inputenc.pkg.json`)

| Option | Description | Status |
|--------|-------------|--------|
| `utf8` | UTF-8 encoding | ✅ Full |
| `latin1` | Latin-1 encoding | 🔶 Partial |

### fontenc (`fontenc.pkg.json`)

| Option | Description | Status |
|--------|-------------|--------|
| `T1` | T1 encoding | 🔶 Partial |
| `OT1` | Original TeX encoding | ✅ Full |

### xparse (`xparse.pkg.json`)

| Command | Description | Status |
|---------|-------------|--------|
| `\NewDocumentCommand` | Define document command | ❌ Missing |
| `\RenewDocumentCommand` | Redefine command | ❌ Missing |
| `\NewDocumentEnvironment` | Define environment | ❌ Missing |

### etoolbox (`etoolbox.pkg.json`)

| Command | Description | Status |
|---------|-------------|--------|
| `\ifdef`, `\ifundef` | Conditional on definition | ❌ Missing |
| `\ifblank`, `\ifstrempty` | String tests | ❌ Missing |
| `\appto`, `\preto` | Append/prepend to macro | ❌ Missing |
| `\AtBeginEnvironment` | Hook at environment start | ❌ Missing |

### textcomp (`textcomp.pkg.json`)

| Command | Description | Status |
|---------|-------------|--------|
| `\texteuro` | Euro symbol € | ✅ Full |
| `\textdegree` | Degree symbol ° | ✅ Full |
| `\textcelsius` | Celsius °C | ✅ Full |
| `\textmu` | Micro symbol μ | ✅ Full |
| `\texttimes` | Multiplication × | ✅ Full |
| `\textdiv` | Division ÷ | ✅ Full |
| `\textpm` | Plus-minus ± | ✅ Full |

---

## Summary Statistics

| Category | Full | Partial | Missing |
|----------|------|---------|---------|
| TeX Base | 15 | 18 | 35+ |
| LaTeX Base | 95 | 25 | 20+ |
| AMS Math | 120+ | 10 | 5 |
| AMS Symbols | 150+ | 0 | 0 |
| Graphics | 3 | 6 | 2 |
| Tables | 6 | 10 | 5 |
| Other Packages | 20 | 40 | 50+ |

**Overall Implementation Status:**
- Core text processing: ✅ Excellent
- Math typesetting: ✅ Excellent  
- Cross-references: ✅ Good
- Tables (basic): ✅ Good
- Graphics: 🔶 Partial
- Page layout: 🔶 Partial
- TikZ/PGF: 🔶 Basic only
- Advanced packages: 🔶 Varies

---

*Last updated: January 2026*
*Source: Lambda TeX pipeline (`lambda/tex/packages/*.pkg.json`)*
