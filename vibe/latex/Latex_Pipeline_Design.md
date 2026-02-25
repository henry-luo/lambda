# DVI and HTML Pipeline Unification

**Last Updated:** January 19, 2026  
**Status:** Reference Document

## Executive Summary

The Lambda TeX implementation uses a **unified pipeline** for all LaTeX-to-output conversions:

```
LaTeX Source → Tree-sitter → DocElement → {HTML, TexNode→DVI/PDF/SVG/PNG}
```

This document is the primary reference for the unified architecture, testing, and debugging.

---

## 1. Unified Architecture

### 1.1 Single Pipeline (Target State)

```
LaTeX Source
     ↓
Tree-sitter Parser (tex_latex_bridge.cpp)
     ↓
Lambda Element AST (Item tree)
     ↓
TexDocumentModel (tex_document_model.cpp)
     ├── DocElement tree (semantic structure)
     │   • PARAGRAPH, HEADING, LIST, TABLE, etc.
     │   • DocTextStyle for formatting
     │   • Cross-references, footnotes, citations
     │   • Macro definitions and expansion
     ↓
     ├── doc_model_to_html() → HTML output
     │
     └── doc_model_to_texnode() → TexNode tree
                                       │
                              ┌────────┼────────┐
                              ↓        ↓        ↓
                             DVI      SVG      PDF/PNG
```

### 1.2 Key Files

| File | Purpose |
|------|---------|
| `tex_latex_bridge.cpp` | Tree-sitter CST → Lambda Element AST |
| `tex_document_model.cpp` | DocElement construction + HTML/TexNode output |
| `tex_document_model.hpp` | DocElement types and structures |
| `tex_math_bridge.cpp` | Math mode → TexNode (inline in DocElement) |
| `tex_linebreak.cpp` | Knuth-Plass line breaking algorithm |
| `tex_pagebreak.cpp` | Page breaking algorithm |
| `tex_dvi_out.cpp` | TexNode → DVI |
| `tex_svg_out.cpp` | TexNode → SVG |
| `tex_pdf_out.cpp` | TexNode → PDF |
| `tex_png_out.cpp` | TexNode → PNG |

### 1.3 DocElement Types

The document model uses these element types (defined in `tex_document_model.hpp`):

- `DOCUMENT` - Root document container
- `PARAGRAPH` - Text paragraphs
- `HEADING` - Section headings (h1-h6)
- `TEXT_SPAN` - Inline text with styling
- `TEXT_RUN` - Raw text content
- `LIST` / `LIST_ITEM` - Ordered/unordered lists
- `TABLE` / `TABLE_ROW` / `TABLE_CELL` - Tables
- `CODE_BLOCK` - Verbatim/code environments
- `MATH_INLINE` / `MATH_DISPLAY` - Math content
- `SPACE` - Whitespace and line breaks
- `RAW_HTML` - Pass-through HTML (for CSS-based features)

## 2. Macro Expansion (UNIFIED ✓)

### 2.1 Current State: Already Unified

Macro expansion is **already unified** in the Tree-sitter pipeline:

```cpp
// In tex_document_model.cpp (lines 178-202):

struct MacroDef {
    const char* name;         // e.g., "\\mycommand"
    int num_args;             // Number of arguments (0-9)
    const char* replacement;  // Replacement text with #1, #2, etc.
    const char* params;       // Parameter pattern: "[]{}[]" etc.
};

void TexDocumentModel::add_macro(const char* name, int num_args, 
                                  const char* replacement, const char* params);
const MacroDef* TexDocumentModel::find_macro(const char* name) const;
```

### 2.2 Macro Registration

When `\newcommand`, `\renewcommand`, or `\def` is encountered:
1. Tree-sitter parses the definition
2. `handle_newcommand()` extracts name, args, replacement
3. `doc->add_macro()` registers in TexDocumentModel

### 2.3 Macro Expansion

When a command is used:
1. `try_expand_macro()` looks up the command (line 2559)
2. Collects arguments from child elements
3. Substitutes `#1`, `#2`, etc. in replacement text
4. Recursively processes the expanded content

**Location:** [tex_document_model.cpp#L2559-L2700](lambda/tex/tex_document_model.cpp#L2559)


## 3. Digestion (UNIFIED ✓)

### 3.1 Current State: Already Unified

"Digestion" (TeX's Stomach processing) is **already unified** in DocElement:

| TeX Concept | DocElement Equivalent |
|-------------|----------------------|
| Mode tracking (H/V/M) | DocElemType (PARAGRAPH, HEADING, MATH_*) |
| Font state | DocTextStyle (family, size, bold, italic) |
| Counter tracking | TexDocumentModel counter system |
| Label/ref resolution | DocElement ref_id + resolve_references() |
| List building | DocElement list_info |
| Box construction | DocElement types (TEXT_SPAN, BLOCK, etc.) |

### 3.2 DocElement as Semantic IR

DocElement serves the same role as DigestedNode but with **semantic** rather than typographic structure:

```cpp
// DocElement types cover all document constructs:
enum class DocElemType {
    PARAGRAPH,       // Text paragraph
    HEADING,         // Section, subsection, etc.
    LIST,            // itemize, enumerate
    LIST_ITEM,       // Individual item
    TABLE,           // tabular, table
    FIGURE,          // figure environment
    MATH_INLINE,     // $...$
    MATH_DISPLAY,    // \[...\] or equation
    TEXT_SPAN,       // Styled text run
    FOOTNOTE,        // Footnote reference
    CROSS_REF,       // \ref, \cite
    // ... etc.
};
```

---

## 4. Remaining Work: doc_model_to_texnode()

### 4.1 Current State

✅ **IMPLEMENTED** - The `doc_model_to_texnode()` and `doc_element_to_texnode()` functions are now implemented at [tex_document_model.cpp#L7850-L8385](lambda/tex/tex_document_model.cpp#L7850).

### 4.2 Implemented Conversions

| DocElemType | Status | Description |
|-------------|--------|-------------|
| `TEXT_RUN` | ✅ | Converts text to Char nodes with TFM metrics |
| `TEXT_SPAN` | ✅ | Styled text with font selection |
| `PARAGRAPH` | ✅ | HList with indentation and parfillskip |
| `HEADING` | ✅ | VList with section number and spacing |
| `LIST` | ✅ | VList with list items |
| `LIST_ITEM` | ✅ | HList with bullet/number and content |
| `MATH_*` | ✅ | Pass-through of pre-typeset TexNode |
| `SPACE` | ✅ | Glue nodes for spacing |
| `DOCUMENT` | ✅ | VList container with parskip |
| `SECTION` | ✅ | VList container |
| `BLOCKQUOTE` | ✅ | VList with margin |

### 4.3 Still TODO

| DocElemType | Status | Notes |
|-------------|--------|-------|
| `TABLE` | ❌ | Requires table layout algorithm |
| `FIGURE` | ❌ | Requires float placement |
| `IMAGE` | ❌ | Requires image loading |
| `CODE_BLOCK` | ❌ | Verbatim text handling |
| `FOOTNOTE` | ❌ | Requires footnote insertion |
| `CITATION` | ❌ | Bibliography formatting |
| `CROSS_REF` | ❌ | Reference text rendering |
| `LINK` | ❌ | Hyperlink handling |

## 5. Output Paths from TexNode

Once `doc_model_to_texnode()` is implemented, all fixed-layout outputs use the same TexNode tree:

```
               TexNode tree
                    │
      ┌─────────────┼─────────────┐
      ↓             ↓             ↓
dvi_output()   svg_output()  pdf_output()
      ↓             ↓             ↓
    .dvi          .svg          .pdf
```

**Already implemented:**
- `dvi_output_node()` in `tex_dvi_out.cpp` ✓
- `svg_output()` in `tex_svg_out.cpp` ✓
- `pdf_output()` in `tex_pdf_out.cpp` ✓
- `png_output()` in `tex_png_out.cpp` ✓

---

## 6. Migration Status

### 6.1 Completed ✓

| Component                 | Status                                            |
| ------------------------- | ------------------------------------------------- |
| Tree-sitter LaTeX grammar | ✓ Implemented                                     |
| Lambda Element AST        | ✓ Implemented                                     |
| TexDocumentModel          | ✓ Implemented                                     |
| DocElement types          | ✓ Comprehensive                                   |
| Macro registration        | ✓ Unified in TexDocumentModel                     |
| Macro expansion           | ✓ Unified via try_expand_macro()                  |
| HTML output               | ✓ doc_model_to_html() complete                    |
| Math to TexNode           | ✓ Inline in DocElement                            |
| SVG/PDF/PNG from TexNode  | ✓ Implemented                                     |
| doc_model_to_texnode()    | ✓ Implemented (basic elements)                    |
| Line breaking integration | ✓ Knuth-Plass via apply_line_breaking_recursive() |
| Page breaking             | ✓ Implemented via doc_model_typeset()             |
| DVI from unified pipeline | ✓ CLI updated to use unified path                 |
| Legacy code removal       | ✓ Deprecated tokenizer/expander/digester removed  |

### 6.2 In Progress

| Component | Status |
|-----------|--------|
| Tables in TexNode | ❌ Requires table layout algorithm |
| Figures in TexNode | ❌ Requires float placement |
| Footnotes in TexNode | ❌ Requires footnote insertion |

### 6.3 Deprecated Code (REMOVED)

These files have been removed from the codebase:

| Component             | Replacement                                             |
| --------------------- | ------------------------------------------------------- |
| `tex_tokenizer.cpp`   | Replaced by Tree-sitter parsing                         |
| `tex_expander.cpp`    | Macro expansion now in TexDocumentModel                 |
| `tex_digester.cpp`    | Document building now in TexDocumentModel               |
| `tex_digested.hpp`    | DigestedNode replaced by DocElement                     |
| `tex_macro.cpp`       | TexDocumentModel.MacroDef                               |
| `tex_conditional.cpp` | Not needed                                              |
| `tex_catcode.cpp`     | Not needed                                              |
| `tex_token.cpp`       | Not needed                                              |
| `tex_package_json.cpp`| Orphaned - never integrated                             |

---

## 7. Working Features (HTML Output)

### 7.1 Text Formatting
- `\textbf{}`, `\textit{}`, `\texttt{}`, `\emph{}`
- `\textsc{}`, `\underline{}`, `\textup{}`, `\textsl{}`
- Font declarations: `\bfseries`, `\itshape`, `\ttfamily`, `\scshape`, `\em`
- Nested formatting with proper scope handling

### 7.2 Document Structure
- `\section{}`, `\subsection{}`, `\subsubsection{}`
- `\chapter{}` (for book class)
- `\paragraph{}`, `\subparagraph{}`
- Preamble handling (`\documentclass`, `\usepackage`)

### 7.3 Environments
- `itemize`, `enumerate`, `description`
- `quote`, `quotation`, `verse`
- `center`, `flushleft`, `flushright`
- `verbatim`, `comment`
- Nested environments

### 7.4 Whitespace & Spacing
- Paragraph breaks (`\par`, blank lines)
- Line breaks (`\\`, `\newline`)
- `\quad`, `\qquad`, `\enspace`, `\enskip`
- `\thinspace`, `\,`, `\negthinspace`
- `\hspace{<length>}` → CSS margin
- Non-breaking space (`~`, `\nobreakspace`)

### 7.5 Special Characters & Symbols
- TeX special chars: `\&`, `\%`, `\$`, `\#`, `\_`, `\{`, `\}`
- Dashes: `--` (en-dash), `---` (em-dash)
- Quotes: `` ` ``, `''`, ``` `` ```, `''`
- Ellipsis: `\ldots`, `\dots`
- Ligatures: `ff`, `fi`, `fl`, `ffi`, `ffl`
- Copyright, trademark, registered symbols
- Special letters: `\ae`, `\oe`, `\ss`, `\o`, `\l`, etc.

### 7.6 Diacritics
- Accents: `\'`, `` \` ``, `\^`, `\"`, `\~`, `\=`, `\.`
- Other marks: `\u`, `\v`, `\H`, `\c`, `\d`, `\b`, `\r`, `\k`

### 7.7 Cross-References
- `\label{name}` - Records label with current section ID and number
- `\ref{name}` - Resolves to `<a href="#sec-N">number</a>`
- `\eqref{name}`, `\pageref{name}` - Also supported
- Two-pass resolution handles forward references

### 7.8 Macros (Basic)
- `\newcommand` without arguments
- `\renewcommand` without arguments
- Simple macro expansion

---

## 8. Hybrid HTML Output Format

Lambda's HTML output follows a **hybrid approach** combining the best of latex.js (typography, compactness) and LaTeXML (semantic structure). The output uses semantic HTML5 tags where appropriate.

### 8.1 Design Principles

- **Semantic HTML5**: Use `<strong>`, `<em>`, `<code>` instead of `<span class="bf">`
- **Compact structure**: Avoid excessive nesting (`<p>` not `<div class="para"><p>`)
- **Meaningful classes**: `class="itemize"` not `class="ltx_itemize"`
- **Proper typography**: Ligatures (`ﬁ`), proper dashes (`–`, `—`), Unicode quotes
- **Working cross-refs**: `<a href="#sec-1">` links that actually navigate

### 8.2 Output Examples

| LaTeX Input | Lambda HTML Output |
|-------------|-------------------|
| Document wrapper | `<article class="latex-document">...</article>` |
| `\textbf{bold}` | `<strong>bold</strong>` |
| `\textit{italic}` | `<em>italic</em>` |
| `\texttt{code}` | `<code>code</code>` |
| `\section{Title}` | `<section class="section"><h2><span class="section-number">1</span> Title</h2>` |
| `\begin{itemize}` | `<ul class="itemize">` |
| `\item text` | `<li><p>text</p></li>` |
| `\begin{quote}` | `<blockquote class="quote">` |
| `\ref{foo}` | `<a href="#foo">1</a>` |
| `--` (en-dash) | `–` (U+2013) |
| `fi` ligature | `ﬁ` (U+FB01) |

### 8.3 Complete Reference

For the full mapping of all LaTeX commands to HTML output, see:
**[Latex_Html_Mapping.md](Latex_Html_Mapping.md)** — comprehensive tables covering document structure, formatting, lists, typography, diacritics, and special characters.

---

## 9. Testing

### 9.1 Running Tests

```bash
# Build all test executables
make build-test

# --- Baseline Tests (must all pass) ---
make test-lambda-baseline    # Lambda core: 73/73 tests
make test-radiant-baseline   # Radiant layout: 1835/1835 tests

# --- DVI Generation Tests ---
./test/test_unified_dvi_gtest.exe    # 7/7 tests
./test/test_tex_dvi_comparison.exe   # test against DVI reference

# --- HTML Generation Tests ---
./test/test_latexml_compare_gtest.exe --gtest_filter="Baseline*"    # Must-pass
./test/test_latexml_compare_gtest.exe --gtest_filter="Extended*"    # Work-in-progress
./test/test_latexml_compare_gtest.exe                               # All tests

# --- Specific test pattern ---
./test/test_latexml_compare_gtest.exe --gtest_filter="*spacing_01*"
```

### 8.2 Test File Locations

| Path | Description |
|------|-------------|
| `test/latexml/fixtures/` | LaTeX test input files (169 files) |
| `test/latexml/expected/` | Hybrid HTML references (`.html` files) |
| `test/latexml/expected/*/*.latexjs.html` | Raw latex.js output (source for hybrid refs) |
| `test_output/latexml/` | Actual test output (generated during tests) |

### 8.3 Regenerating Hybrid References

```bash
# Regenerate all hybrid references from latexjs/latexml sources
python3 utils/generate_hybrid_refs.py --clean --verbose
```

### 8.4 Adding New Baseline Tests

When a test passes consistently, add it to the baseline set in `test/test_latexml_compare_gtest.cpp`:

```cpp
static const std::set<std::string> BASELINE_FIXTURES = {
    // ... existing entries ...
    "latexjs/category/##_test_name",  // Full path from fixtures/
};
```

---

## 10. Debugging

### 9.1 Manual conversion testing

```bash
# Convert LaTeX file to HTML
./lambda.exe convert input.tex -f latex -t html -o output.html

# Convert LaTeX to DVI
./lambda.exe dvi input.tex -o output.dvi
```
### 9.2 Output parsed AST

Output parsed AST in Mark format (useful for debugging parser output)
```bash
./lambda.exe convert input.tex -f latex -t mark -o /tmp/ast.mark
cat /tmp/ast.mark
```

### 9.3 Debug Logging

Add debug output in `lambda/tex/tex_document_model.cpp`:
```cpp
log_debug("tag: %s, processing element", tag);
```

View logs:
```bash
cat log.txt | grep "your_prefix"
```

### 9.4 Key Debugging Points

1. **Parser Output**: Check what the Tree-sitter parser produces
   ```bash
   grep "convert_latex_node" log.txt
   ```

2. **Element Tags**: The document model dispatches on element tag names
   ```cpp
   if (tag_eq(tag, "your_command")) {
       // Handle command
   }
   ```

3. **Package Definitions**: Check if command is defined
   ```bash
   grep "your_command" lambda/tex/packages/*.pkg.json
   ```

4. **Build Verification**:
   ```bash
   make build 2>&1 | grep -E "error:|warning:.*tex_"
   ```

---

## 11. Outstanding Issues

### 10.1 Extended Test Categories (Not Yet Passing)

| Category | Tests | Issue |
|----------|-------|-------|
| `boxes/` | 4 | `\parbox` not implemented |
| `counters/` | 2 | Counter commands not implemented |
| `fonts/04-08` | 5 | Font scope across paragraphs, ligature suppression |
| `groups/03` | 1 | Groups overlapping paragraphs |
| `layout-marginpar/` | 3 | Page layout, margin notes |
| `macros/02-06` | 5 | Macros with arguments |
| `spacing/01-04` | 4 | Minor whitespace differences |

### 10.2 Priority Action Items

1. **Macros with Arguments** (High Priority)
   - Implement `\newcommand` with mandatory arguments `#1`, `#2`, etc.
   - Implement optional arguments `[#1]`
   - File: `lambda/tex/tex_document_model.cpp`

2. **Vertical Spacing** (Medium Priority)
   - `\vspace{<length>}` with CSS `margin-bottom`
   - `\smallskip`, `\medskip`, `\bigskip`

3. **Complex TexNode Elements** (Medium Priority)
   - Tables, figures, footnotes in DVI output

4. **Counters** (Low Priority)
   - `\newcounter`, `\setcounter`, `\stepcounter`
   - `\arabic{}`, `\roman{}`, `\Roman{}`

---

## 12. Summary

### Architecture Decisions

1. **Single front-end:** Tree-sitter parser (no tokenizer)
2. **Single IR:** DocElement (no DigestedNode)
3. **Unified macros:** TexDocumentModel.MacroDef (no Expander)
4. **Shared typesetting:** TexNode tree for all fixed-layout output
5. **Unified CLI:** `./lambda.exe dvi` uses `doc_model_typeset()` path

### Key Benefits

1. **Single codebase** - One parser, one IR, multiple outputs
2. **No duplication** - Macro expansion, counter tracking, refs all unified
3. **Easier testing** - Test DocElement once, outputs follow
4. **Simpler maintenance** - Fix bugs in one place

---

## Appendix 

### A: Unit Conversion Reference

For `\hspace{<length>}` and similar commands:

| Unit | Pixels | Formula |
|------|--------|---------|
| pt | 96/72 = 1.333 | 1pt = 1/72 inch |
| cm | 96/2.54 = 37.795 | 1cm = 1/2.54 inch |
| mm | 96/25.4 = 3.7795 | 1mm = 1/25.4 inch |
| in | 96 | 1in = 96px (CSS standard) |
| em | 16 | Assumed 16px base font |

### B: Typesetting Pipeline Functions

```cpp
// Main entry point for typesetting
void doc_model_typeset(TexDocumentModel* doc, const TypesetParams& params);

// Line breaking parameters (Knuth-Plass)
struct LineBreakParams {
    int32_t hsize;              // Line width in scaled points
    int32_t tolerance;          // Badness tolerance (default 200)
    int32_t pretolerance;       // First-pass tolerance (default 100)
    int32_t line_penalty;       // Penalty per line (default 10)
    int32_t hyphen_penalty;     // Hyphenation penalty (default 50)
    int32_t exhyphen_penalty;   // Explicit hyphen penalty (default 50)
    int32_t double_hyphen_demerits;
    int32_t final_hyphen_demerits;
    int32_t adj_demerits;
    int32_t looseness;
};

// Page breaking parameters
struct PageBreakParams {
    int32_t page_height;        // In scaled points
    int32_t widow_penalty;
    int32_t club_penalty;
    int32_t broken_penalty;
};
```
