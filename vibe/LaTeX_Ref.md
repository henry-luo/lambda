# LaTeX Language Reference

A comprehensive reference guide to LaTeX language features, organized by category.

## Document Structure

### Document Classes
| Syntax | Description |
|--------|-------------|
| `\documentclass{article}` | Standard article format |
| `\documentclass{book}` | Book format with chapters |
| `\documentclass{report}` | Report format |
| `\documentclass{letter}` | Letter format |
| `\documentclass{beamer}` | Presentation slides |

### Document Environment
| Syntax | Description |
|--------|-------------|
| `\begin{document}` | Start document content |
| `\end{document}` | End document content |
| `\title{Title}` | Set document title |
| `\author{Name}` | Set document author |
| `\date{Date}` | Set document date |
| `\maketitle` | Generate title page |

### Packages
| Syntax | Description |
|--------|-------------|
| `\usepackage{package}` | Include a package |
| `\usepackage[options]{package}` | Include package with options |
| `\RequirePackage{package}` | Require package (for class files) |

## Text Formatting

### Font Styles
| Syntax | Description |
|--------|-------------|
| `\textbf{text}` | Bold text |
| `\textit{text}` | Italic text |
| `\texttt{text}` | Typewriter/monospace text |
| `\textsc{text}` | Small caps |
| `\emph{text}` | Emphasized text (usually italic) |
| `\underline{text}` | Underlined text |
| `\textsl{text}` | Slanted text |

### Font Sizes
| Syntax | Description |
|--------|-------------|
| `\tiny` | Smallest font size |
| `\scriptsize` | Very small font |
| `\footnotesize` | Small font |
| `\small` | Small font |
| `\normalsize` | Normal font size |
| `\large` | Large font |
| `\Large` | Larger font |
| `\LARGE` | Very large font |
| `\huge` | Huge font |
| `\Huge` | Largest font size |

### Text Alignment
| Syntax | Description |
|--------|-------------|
| `\begin{center}...\end{center}` | Center text |
| `\begin{flushleft}...\end{flushleft}` | Left-align text |
| `\begin{flushright}...\end{flushright}` | Right-align text |
| `\centering` | Center following text |
| `\raggedright` | Left-align following text |
| `\raggedleft` | Right-align following text |

## Document Structure Elements

### Sectioning
| Syntax | Description |
|--------|-------------|
| `\part{title}` | Part (highest level) |
| `\chapter{title}` | Chapter (books/reports only) |
| `\section{title}` | Section |
| `\subsection{title}` | Subsection |
| `\subsubsection{title}` | Sub-subsection |
| `\paragraph{title}` | Paragraph heading |
| `\subparagraph{title}` | Sub-paragraph heading |

### Table of Contents
| Syntax | Description |
|--------|-------------|
| `\tableofcontents` | Generate table of contents |
| `\listoffigures` | Generate list of figures |
| `\listoftables` | Generate list of tables |
| `\section*{title}` | Unnumbered section |
| `\addcontentsline{toc}{level}{title}` | Add entry to TOC |

## Lists

### Itemized Lists
| Syntax | Description |
|--------|-------------|
| `\begin{itemize}...\end{itemize}` | Bulleted list |
| `\begin{enumerate}...\end{enumerate}` | Numbered list |
| `\begin{description}...\end{description}` | Description list |
| `\item` | List item |
| `\item[label]` | Item with custom label |

### Custom Lists
| Syntax | Description |
|--------|-------------|
| `\renewcommand{\labelitemi}{symbol}` | Change bullet symbol |
| `\setcounter{enumi}{number}` | Set enumerate counter |
| `\begin{enumerate}[a)]` | Custom numbering (with enumitem) |

## Mathematics

### Math Modes
| Syntax | Description |
|--------|-------------|
| `$...$` | Inline math |
| `\(...\)` | Inline math (alternative) |
| `$$...$$` | Display math (plain TeX) |
| `\[...\]` | Display math |
| `\begin{equation}...\end{equation}` | Numbered equation |
| `\begin{equation*}...\end{equation*}` | Unnumbered equation |

### Math Environments
| Syntax | Description |
|--------|-------------|
| `\begin{align}...\end{align}` | Aligned equations |
| `\begin{gather}...\end{gather}` | Gathered equations |
| `\begin{split}...\end{split}` | Split long equation |
| `\begin{cases}...\end{cases}` | Case-by-case definitions |
| `\begin{matrix}...\end{matrix}` | Matrix without delimiters |
| `\begin{pmatrix}...\end{pmatrix}` | Matrix with parentheses |
| `\begin{bmatrix}...\end{bmatrix}` | Matrix with brackets |

### Math Symbols
| Syntax | Description |
|--------|-------------|
| `\alpha, \beta, \gamma` | Greek letters |
| `\sum, \prod, \int` | Large operators |
| `\frac{num}{den}` | Fraction |
| `\sqrt{x}` | Square root |
| `\sqrt[n]{x}` | nth root |
| `x^{sup}` | Superscript |
| `x_{sub}` | Subscript |
| `\infty` | Infinity |
| `\pm, \mp` | Plus/minus, minus/plus |
| `\leq, \geq` | Less/greater than or equal |
| `\neq` | Not equal |

### Math Functions
| Syntax | Description |
|--------|-------------|
| `\sin, \cos, \tan` | Trigonometric functions |
| `\log, \ln` | Logarithmic functions |
| `\exp` | Exponential function |
| `\lim` | Limit |
| `\max, \min` | Maximum, minimum |
| `\gcd` | Greatest common divisor |

## Tables and Figures

### Tables
| Syntax | Description |
|--------|-------------|
| `\begin{tabular}{cols}...\end{tabular}` | Basic table |
| `\begin{table}...\end{table}` | Floating table environment |
| `c, l, r` | Column alignment (center, left, right) |
| `p{width}` | Paragraph column with fixed width |
| `&` | Column separator |
| `\\` | Row separator |
| `\hline` | Horizontal line |
| `\cline{i-j}` | Partial horizontal line |
| `\multicolumn{n}{align}{text}` | Span multiple columns |
| `\multirow{n}{width}{text}` | Span multiple rows |

### Figures
| Syntax | Description |
|--------|-------------|
| `\begin{figure}...\end{figure}` | Floating figure environment |
| `\includegraphics{file}` | Include image |
| `\includegraphics[options]{file}` | Include image with options |
| `\caption{text}` | Figure/table caption |
| `\label{key}` | Label for referencing |
| `\ref{key}` | Reference to label |

### Positioning
| Syntax | Description |
|--------|-------------|
| `[h]` | Here (approximately) |
| `[t]` | Top of page |
| `[b]` | Bottom of page |
| `[p]` | Separate page |
| `[!]` | Override restrictions |
| `[H]` | Exactly here (with float package) |

## Cross-References and Citations

### Labels and References
| Syntax | Description |
|--------|-------------|
| `\label{key}` | Create label |
| `\ref{key}` | Reference number |
| `\pageref{key}` | Reference page number |
| `\eqref{key}` | Reference equation (with parentheses) |
| `\autoref{key}` | Automatic reference (with hyperref) |

### Citations
| Syntax | Description |
|--------|-------------|
| `\cite{key}` | Basic citation |
| `\cite[page]{key}` | Citation with page number |
| `\citep{key}` | Parenthetical citation (natbib) |
| `\citet{key}` | Textual citation (natbib) |
| `\bibliography{file}` | Include bibliography |
| `\bibliographystyle{style}` | Set bibliography style |

## Environments

### Text Environments
| Syntax | Description |
|--------|-------------|
| `\begin{quote}...\end{quote}` | Short quotation |
| `\begin{quotation}...\end{quotation}` | Long quotation |
| `\begin{verse}...\end{verse}` | Poetry |
| `\begin{verbatim}...\end{verbatim}` | Verbatim text |
| `\verb|text|` | Inline verbatim |

### Theorem Environments
| Syntax | Description |
|--------|-------------|
| `\newtheorem{name}{title}` | Define theorem environment |
| `\begin{theorem}...\end{theorem}` | Theorem |
| `\begin{lemma}...\end{lemma}` | Lemma |
| `\begin{proof}...\end{proof}` | Proof |
| `\begin{definition}...\end{definition}` | Definition |

## Spacing and Layout

### Horizontal Spacing
| Syntax | Description |
|--------|-------------|
| `\ ` | Normal space |
| `\,` | Thin space |
| `\:` | Medium space |
| `\;` | Thick space |
| `\!` | Negative thin space |
| `\quad` | 1em space |
| `\qquad` | 2em space |
| `\hspace{length}` | Custom horizontal space |
| `\hfill` | Fill horizontal space |

### Vertical Spacing
| Syntax | Description |
|--------|-------------|
| `\\` | Line break |
| `\\[length]` | Line break with extra space |
| `\vspace{length}` | Vertical space |
| `\vfill` | Fill vertical space |
| `\newpage` | Start new page |
| `\clearpage` | Clear page and process floats |
| `\pagebreak` | Page break |
| `\linebreak` | Line break |

### Page Layout
| Syntax | Description |
|--------|-------------|
| `\newline` | New line |
| `\noindent` | No paragraph indentation |
| `\indent` | Force paragraph indentation |
| `\par` | Paragraph break |

## Special Characters and Symbols

### Reserved Characters
| Syntax | Description |
|--------|-------------|
| `\$` | Dollar sign |
| `\%` | Percent sign |
| `\&` | Ampersand |
| `\_` | Underscore |
| `\{` | Left brace |
| `\}` | Right brace |
| `\#` | Hash/number sign |
| `\^{}` | Caret |
| `\~{}` | Tilde |
| `\textbackslash` | Backslash |

### Common Symbols
| Syntax | Description |
|--------|-------------|
| `\copyright` | Copyright symbol |
| `\textregistered` | Registered trademark |
| `\texttrademark` | Trademark |
| `\textdegree` | Degree symbol |
| `\S` | Section symbol |
| `\P` | Paragraph symbol |
| `\dag` | Dagger |
| `\ddag` | Double dagger |

### Accents and Special Characters
| Syntax | Description |
|--------|-------------|
| `\'a` | Acute accent |
| `\`a` | Grave accent |
| `\^a` | Circumflex |
| `\"a` | Umlaut |
| `\~a` | Tilde |
| `\=a` | Macron |
| `\.a` | Dot accent |
| `\c{c}` | Cedilla |

## Advanced Features

### Custom Commands
| Syntax | Description |
|--------|-------------|
| `\newcommand{\name}{definition}` | Define new command |
| `\newcommand{\name}[n]{definition}` | Command with n parameters |
| `\renewcommand{\name}{definition}` | Redefine existing command |
| `\providecommand{\name}{definition}` | Define if not already defined |

### Counters
| Syntax | Description |
|--------|-------------|
| `\newcounter{name}` | Create new counter |
| `\setcounter{name}{value}` | Set counter value |
| `\addtocounter{name}{value}` | Add to counter |
| `\stepcounter{name}` | Increment counter |
| `\value{name}` | Get counter value |
| `\arabic{name}` | Arabic numerals |
| `\roman{name}` | Lowercase roman |
| `\Roman{name}` | Uppercase roman |

### Conditionals
| Syntax | Description |
|--------|-------------|
| `\ifthenelse{test}{true}{false}` | Conditional (ifthen package) |
| `\ifdefined\command` | Test if command defined |
| `\ifx\a\b` | Test if two tokens equal |

## Common Packages

### Essential Packages
| Package | Description |
|---------|-------------|
| `amsmath` | Enhanced math environments |
| `amsfonts` | Additional math fonts |
| `amssymb` | Additional math symbols |
| `graphicx` | Enhanced graphics support |
| `hyperref` | Hyperlinks and bookmarks |
| `geometry` | Page layout customization |
| `fancyhdr` | Custom headers and footers |

### Specialized Packages
| Package | Description |
|---------|-------------|
| `babel` | Multilingual support |
| `inputenc` | Input encoding |
| `fontenc` | Font encoding |
| `microtype` | Microtypography |
| `booktabs` | Professional tables |
| `longtable` | Multi-page tables |
| `enumitem` | Customize lists |
| `listings` | Code listings |
| `tikz` | Graphics and diagrams |
| `beamer` | Presentations |

## Best Practices

### General Guidelines
- Use `\label{}` and `\ref{}` for cross-references
- Place figures and tables in floating environments
- Use semantic markup (e.g., `\emph{}` instead of `\textit{}`)
- Load packages in logical order
- Use consistent spacing and formatting
- Comment complex code with `%`

### Common Mistakes to Avoid
- Don't use `$$...$$` in LaTeX (use `\[...\]`)
- Don't use bare `&` or `%` (escape with `\&` or `\%`)
- Don't use manual spacing instead of proper environments
- Don't hardcode cross-references
- Don't use deprecated commands like `\bf` or `\it`

---

*This reference covers the most commonly used LaTeX features. For advanced topics and package-specific commands, consult the relevant package documentation.*
