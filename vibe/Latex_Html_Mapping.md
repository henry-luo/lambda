# LaTeX to HTML Mapping Decisions

This document captures the design decisions for Lambda's TeX-to-HTML pipeline, based on comparative analysis of latex.js and LaTeXML output.

## Overview

Lambda's TeX pipeline converts LaTeX documents to HTML. Two established converters were analyzed:

- **latex.js**: JavaScript-based, presentation-focused, mimics rendered PDF appearance
- **LaTeXML**: Perl-based, document-model focused, preserves logical structure

Lambda adopts a **hybrid approach**, taking the best of both while leveraging semantic HTML5.

## High-Level Differences

### latex.js Philosophy
- Flat document structure with single `<div class="body">` wrapper
- Short CSS class names (`bf`, `it`, `tt`)
- Focuses on visual output fidelity
- Handles typographic niceties (ligatures, proper dashes, guillemets)
- Cross-references produce working anchor links

### LaTeXML Philosophy  
- Deep nesting with `<section>` and `<div class="ltx_para">` per paragraph
- Verbose CSS class names with `ltx_` prefix
- Preserves document semantics and hierarchy
- Uses semantic HTML (`<blockquote>`, roles for accessibility)
- Auto-generates structured IDs (`Ch1.S1.p1`)

### Lambda Hybrid Approach
Lambda combines strengths of both:
- **Structure**: Semantic sections from LaTeXML
- **Typography**: Proper Unicode from latex.js
- **HTML tags**: Semantic HTML5 (`<strong>`, `<em>`, `<code>`)
- **Output size**: Compact like latex.js
- **Cross-references**: Working anchor links like latex.js

---

## Detailed Mapping Table

### Document Structure

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| Document wrapper | `<div class="body">...</div>` | `<article class="ltx_document">...</article>` | latex.js (compact) | `<article class="latex-document">...</article>` |
| Paragraph | `<p>text</p>` | `<div class="ltx_para"><p class="ltx_p">text</p></div>` | latex.js (simpler) | `<p>text</p>` |
| `\noindent` | `<p class="noindent">` | `<div class="ltx_para ltx_noindent"><p class="ltx_p">` | latex.js | `<p class="noindent">` |
| Paragraph continuation | `<p class="continue">` | Plain `<p class="ltx_p">` | latex.js | `<p class="continue">` |

### Sectioning

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\chapter{Title}` | `<h1 id="sec-1"><div>Chapter 1</div>Title</h1>` | `<section class="ltx_chapter"><h2><span class="ltx_tag">Chapter 1</span>Title</h2>` | LaTeXML (semantic) | `<section class="chapter"><h1><span class="chapter-number">Chapter 1</span> Title</h1>` |
| `\section{Title}` | `<h2 id="sec-2">1.1 Title</h2>` | `<section class="ltx_section"><h3><span class="ltx_tag">1.1</span>Title</h3>` | LaTeXML (semantic) | `<section class="section"><h2><span class="section-number">1.1</span> Title</h2>` |
| `\subsection{Title}` | `<h3>` | `<section class="ltx_subsection"><h4>` | LaTeXML | `<section class="subsection"><h3>` |
| Section IDs | `id="sec-1"` (sequential) | `id="Ch1.S1"` (hierarchical) | LaTeXML (structured) | `id="sec-1"` or auto-generated |

### Text Formatting

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\textbf{bold}` | `<span class="bf">bold</span>` | `<span class="ltx_text ltx_font_bold">bold</span>` | Tie | `<strong>bold</strong>` |
| `\textit{italic}` | `<span class="it">italic</span>` | `<span class="ltx_text ltx_font_italic">italic</span>` | Tie | `<em>italic</em>` |
| `\texttt{mono}` | `<span class="tt">mono</span>` | `<span class="ltx_text ltx_font_typewriter">mono</span>` | Tie | `<code>mono</code>` |
| `\emph{text}` | `<span class="it">text</span>` | `<span class="ltx_text ltx_font_italic">text</span>` | Tie | `<em>text</em>` |
| `\underline{text}` | `<span class="underline">text</span>` | `<span class="ltx_ERROR">text</span>` | latex.js | `<u>text</u>` |
| `\sout{text}` | `<span class="sout">text</span>` | `<span class="ltx_ERROR">text</span>` | latex.js | `<s>text</s>` |

### Font Sizes

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\tiny{text}` | `<span class="tiny">text</span>` | `<span class="ltx_text">text</span>` | latex.js | `<span class="tiny">text</span>` |
| `\small{text}` | `<span class="small">text</span>` | `<span class="ltx_text">text</span>` | latex.js | `<span class="small">text</span>` |
| `\large{text}` | `<span class="large">text</span>` | `<span class="ltx_text">text</span>` | latex.js | `<span class="large">text</span>` |
| `\Large{text}` | `<span class="Large">text</span>` | `<span class="ltx_text">text</span>` | latex.js | `<span class="Large">text</span>` |
| `\huge{text}` | `<span class="huge">text</span>` | `<span class="ltx_text">text</span>` | latex.js | `<span class="huge">text</span>` |

### Lists

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\begin{itemize}` | `<ul class="list">` | `<ul class="ltx_itemize">` | Tie | `<ul class="itemize">` |
| `\begin{enumerate}` | `<ol class="list">` | `<ol class="ltx_enumerate">` | Tie | `<ol class="enumerate">` |
| `\item text` | `<li><span class="itemlabel"><span class="hbox llap">•</span></span><p>text</p></li>` | `<li class="ltx_item" style="list-style-type:none;"><span class="ltx_tag">•</span><div class="ltx_para"><p>text</p></div></li>` | latex.js (simpler label) | `<li><p>text</p></li>` (bullet via CSS) |
| `\item[label] text` | `<li><span class="itemlabel"><span class="hbox llap">label</span></span><p>text</p></li>` | `<li><span class="ltx_tag">label</span>...` | latex.js | `<li data-label="label"><p>text</p></li>` |
| `\begin{description}` | `<dl class="list">` | `<dl class="ltx_description">` | Tie | `<dl class="description">` |
| `\item[term] def` | `<dt>term</dt><dd><p>def</p></dd>` | `<dt><span class="ltx_tag"><span class="ltx_font_bold">term</span></span></dt><dd>...` | latex.js (cleaner) | `<dt>term</dt><dd><p>def</p></dd>` |

### Quote Environments

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\begin{quote}` | `<div class="list quote">` | `<blockquote class="ltx_quote">` | LaTeXML (semantic) | `<blockquote class="quote">` |
| `\begin{quotation}` | `<div class="list quotation">` | `<blockquote class="ltx_quote">` | LaTeXML | `<blockquote class="quotation">` |
| `\begin{verse}` | `<div class="list verse">` | `<blockquote class="ltx_quote ltx_role_verse">` | LaTeXML | `<blockquote class="verse">` |

### Alignment

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\begin{center}` | `<div class="list center"><p>...</p></div>` | `<p class="ltx_p ltx_align_center">...</p>` | LaTeXML (no wrapper) | `<p class="centering">...</p>` or `<div class="center">` |
| `\begin{flushleft}` | `<div class="list flushleft">` | `<p class="ltx_align_left">` | LaTeXML | `<p class="raggedright">` |
| `\begin{flushright}` | `<div class="list flushright">` | `<p class="ltx_align_right">` | LaTeXML | `<p class="raggedleft">` |
| `\centering` | `<p class="centering">` | `<p class="ltx_centering">` | Tie | `<p class="centering">` |

### Cross-References

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\label{foo}` | `<a id="foo"></a>` | (absorbed into parent element) | latex.js (explicit) | `<a id="foo"></a>` or attribute |
| `\ref{foo}` | `<a href="#foo">1</a>` | `<span class="ltx_ref ltx_ref_self">id1</span>` | latex.js (working link) | `<a href="#foo">1</a>` |
| `\pageref{foo}` | `<a href="#foo">1</a>` | `<span class="ltx_ref">` | latex.js | `<a href="#foo">1</a>` (page N/A in HTML) |

### Verbatim/Code

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\verb\|code\|` | `<code class="tt">code</code>` | `<code class="ltx_verbatim ltx_font_typewriter">code</code>` | Tie | `<code>code</code>` |
| `\verb*\|a b\|` | `<code class="tt">a␣b</code>` (visible space) | `<code>a␣b</code>` | Tie | `<code>a␣b</code>` |
| `\begin{verbatim}` | `<pre class="verbatim">` | `<pre class="ltx_verbatim">` | Tie | `<pre class="verbatim">` |

### Typography & Special Characters

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `fi`, `fl`, `ff` | `ﬁ`, `ﬂ`, `ﬀ` (ligatures) | `fi`, `fl`, `ff` (plain) | latex.js | `ﬁ`, `ﬂ`, `ﬀ` (ligatures) |
| `--` (en-dash) | `–` (U+2013) | `–` | Tie | `–` (U+2013) |
| `---` (em-dash) | `—` (U+2014) | `—` | Tie | `—` (U+2014) |
| `-` (hyphen) | `‐` (U+2010) | `-` (ASCII) | latex.js | `‐` (U+2010) |
| `<<` `>>` (guillemets) | `«` `»` | `¡¡` `¿¿` (broken) | latex.js | `«` `»` |
| `!`` (inverted !) | `¡` | `!´` (broken) | latex.js | `¡` |
| `?`` (inverted ?) | `¿` | `?´` (broken) | latex.js | `¿` |
| `\,` (thin space) | ` ` (U+2009) | ` ` (regular space) | latex.js | ` ` (U+2009) |
| `~` (non-breaking) | `&nbsp;` | `&nbsp;` or ` ` | Tie | `&nbsp;` |

### Diacritics

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\'e` | `é` | `é` | Tie | `é` |
| `\`e` | `è` | `è` | Tie | `è` |
| `\"o` | `ö` | `ö` | Tie | `ö` |
| `\^o` | `ô` | `ô` | Tie | `ô` |
| `\~n` | `ñ` | `ñ` | Tie | `ñ` |
| `\o` | `ø` | `ø` | Tie | `ø` |
| `\ss` | `ß` | `ß` | Tie | `ß` |

### Logos

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\TeX` | `<span class="tex">T<span class="e">e</span>X</span>` | `<span class="ltx_TeX_logo" style="...">T<span style="...">e</span>X</span>` | latex.js (CSS classes) | `<span class="tex-logo">T<span class="e">e</span>X</span>` |
| `\LaTeX` | `<span class="latex">L<span class="a">a</span>T<span class="e">e</span>X</span>` | `<span class="ltx_LaTeX_logo" style="...">...</span>` | latex.js (CSS classes) | `<span class="latex-logo">L<span class="a">a</span>T<span class="e">e</span>X</span>` |

### Line Breaks

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\\` | `<br>` | `<br class="ltx_break">` | Tie | `<br>` |
| `\newline` | `<br>` | `<br class="ltx_break">` | Tie | `<br>` |
| `\par` | New `<p>` element | New `<div class="ltx_para"><p>` | latex.js | New `<p>` element |

### Whitespace Boundaries

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `{}` (empty group) | `​` (ZWSP U+200B) | (nothing) | latex.js (explicit) | `​` (ZWSP) |
| `\mbox{}` | `<span class="hbox"><span></span></span>` | (nothing) | latex.js | `<span class="hbox"></span>` |
| `\/` (italic correction) | `‌` (ZWNJ U+200C) | (nothing) | latex.js | `‌` (ZWNJ) |

### Links and URLs

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\href{url}{text}` | `<a href="url">text</a>` | `<a href="url" class="ltx_ref">text</a>` | Tie | `<a href="url">text</a>` |
| `\url{url}` | `<a href="url" class="tt">url</a>` | `<a href="url" class="ltx_url">url</a>` | Tie | `<a href="url" class="url">url</a>` |

### Images

| TeX Input | latex.js Output | LaTeXML Output | Winner | Lambda Output |
|-----------|-----------------|----------------|--------|---------------|
| `\includegraphics{img}` | `<img src="img">` | `<img src="img" class="ltx_graphics">` | Tie | `<img src="img" class="graphics">` |
| `\includegraphics[width=5cm]{img}` | `<img src="img" style="width:5cm">` | `<img src="img" style="width:...">` | Tie | `<img src="img" style="width:5cm">` |

---

## Output Mode Summary

Lambda supports multiple output modes via `HtmlOutputOptions`:

| Mode | Use Case | Characteristics |
|------|----------|-----------------|
| **Legacy** | latex.js fixture compatibility | `<div class="body">`, `<span class="bf">`, etc. |
| **Modern** (default) | Semantic web documents | `<article>`, `<strong>`, `<em>`, `<code>` |
| **LaTeXML-compatible** | Academic publishing | `ltx_` class prefix, nested sections |

---

## Test Infrastructure

### Directory Structure

All LaTeX tests are consolidated under `test/latex/`:

```
test/latex/
├── fixtures/           # Source .tex files organized by category
│   ├── basic/          # Basic text and formatting
│   ├── math/           # Mathematical expressions
│   │   └── subjects/   # Subject-specific math (calculus, physics, etc.)
│   ├── latexjs/        # latex.js-specific fixtures (fragments)
│   ├── fonts/          # Font handling tests
│   ├── structure/      # Document structure (sections, lists)
│   ├── graphics/       # Images and graphics
│   ├── tikz/           # TikZ diagrams
│   └── ...             # Other categories (align, ams, boxes, etc.)
├── expected/           # Reference output files
│   ├── basic/          # HTML and DVI references matching fixtures
│   ├── math/
│   │   └── subjects/
│   ├── latexjs/
│   └── ...
└── run_tex_tests.sh    # Legacy test runner
```

**Naming convention:**
- Fixture: `fixtures/<category>/<name>.tex`
- HTML reference: `expected/<category>/<name>.html`
- DVI reference: `expected/<category>/<name>.dvi`

### Reference Generation Script

The unified script `utils/generate_latex_refs.js` generates both HTML and DVI references:

```bash
# Generate HTML references (default)
node utils/generate_latex_refs.js

# Generate DVI references
node utils/generate_latex_refs.js --output-format=dvi

# Options
node utils/generate_latex_refs.js --force           # Regenerate all
node utils/generate_latex_refs.js --verbose         # Show details
node utils/generate_latex_refs.js --test=matrix     # Filter by pattern
node utils/generate_latex_refs.js --test-dir=dir    # Custom test directory
node utils/generate_latex_refs.js --clean           # Remove existing first
node utils/generate_latex_refs.js --dry-run         # Preview actions
```

**HTML generation logic:**
- **latexjs/** fixtures: Uses latex.js → transforms to Lambda hybrid HTML
- **Other fixtures**: Uses LaTeXML → transforms to Lambda hybrid HTML

**DVI generation:**
- Uses `latex` command from MacTeX
- Requires `\documentclass` — fragment files (like latexjs/) will fail
- Generated DVI files are used for glyph-level comparison tests

**Requirements:**
```bash
# For HTML generation
cd latex-js && npm install --legacy-peer-deps && npm run build
brew install latexml

# For DVI generation
brew install --cask mactex
```

### Test Executables

| Test | Purpose | Command |
|------|---------|---------|
| HTML comparison | Compare Lambda HTML output vs references | `./test/test_latex_html_compare_gtest.exe` |
| DVI comparison | Compare Lambda DVI output vs references | `./test/test_latex_dvi_compare_gtest.exe` |

**Running tests:**
```bash
# All baseline tests
make test-latex-baseline

# Specific test suites
./test/test_latex_html_compare_gtest.exe --gtest_filter="*Baseline*"
./test/test_latex_dvi_compare_gtest.exe --gtest_filter="DVICompareBaselineTest.*"
```

---

## References

- latex.js: https://github.com/nickvdw/latex.js
- LaTeXML: https://dlmf.nist.gov/LaTeXML/
- Lambda test fixtures: `test/latex/fixtures/`
- Expected outputs: `test/latex/expected/`
- Reference generator: `utils/generate_latex_refs.js`
