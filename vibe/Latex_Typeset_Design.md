# LaTeX/TeX Typeset Design

This document summarizes the LaTeX/TeX typesetting support in Lambda/Radiant.

## Key Design Decisions

### 1. Tree-sitter Based Parsing

The system uses Tree-sitter for parsing LaTeX source files, providing:

- **Robust incremental parsing**: Fast re-parsing on edits
- **Error recovery**: Partial parsing even with syntax errors
- **Grammar location**: `lambda/tree-sitter-latex/grammar.js`
- **External scanner**: Handles verbatim content, `\begin{document}`, `\end{document}`, `\verb`, `\char`, and TeX caret notation (`^^XX`)

Key grammar features:
```javascript
// Design principles from grammar.js:
// 1. Match LaTeX.js structure for compatibility
// 2. Use generic command/macro handling (semantic interpretation at runtime)
// 3. Keep specialized rules only where parsing behavior differs
// 4. Use external scanner for verbatim content
```

### 2. Separate Math Grammar

Math mode parsing is factored into a separate Tree-sitter grammar (`lambda/tree-sitter-latex-math/grammar.js`) enabling:

- **Standalone math parsing**: Math can be parsed independently (e.g., for math in Markdown, MathML conversion)
- **Dedicated optimization**: Math-specific precedence and disambiguation
- **Cleaner separation**: Text mode vs. math mode rules don't interfere

Math grammar handles:
- Fractions (`\frac`), radicals (`\sqrt`), binomials
- Sub/superscripts with proper attachment
- Delimiters (`\left`, `\right`)
- Operators, relations, accents
- Math environments (equation, align, etc.)

### 3. Dual Font System (TFM + TrueType)

The system supports both TeX Font Metrics (TFM) and TrueType fonts:

| Font Provider | Use Case | Implementation |
|---------------|----------|----------------|
| **TFMFontProvider** | DVI output, high-fidelity TeX metrics | `tex_tfm.hpp`, `tex_font_adapter.hpp` |
| **FreeTypeFontProvider** | Screen rendering, SVG/PNG output | `tex_font_adapter.hpp` |

TFM support includes:
- Character metrics (width, height, depth, italic correction)
- Ligature tables (fi, fl, ff, ffi, ffl)
- Kerning information
- Math font parameters (axis height, rule thickness, etc.)
- Extensible character recipes (for large delimiters)

The `FontProvider` interface abstracts both backends:
```cpp
const FontMetrics* get_font(FontFamily family, bool bold, bool italic, float size_pt);
const FontMetrics* get_math_symbol_font(float size_pt);
const FontMetrics* get_math_extension_font(float size_pt);
```

---

## Pipeline Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          LaTeX/TeX Typesetting Pipeline                     │
└─────────────────────────────────────────────────────────────────────────────┘

  LaTeX Source (.tex)
        │
        ▼
┌───────────────────┐
│  Tree-sitter      │  grammar: tree-sitter-latex/grammar.js
│  LaTeX Parser     │  implementation: input-latex-ts.cpp
└───────────────────┘
        │
        ▼
┌───────────────────┐
│  Lambda AST       │  Mark/Element tree (Lambda's universal data model)
│  (Mark Tree)      │  Can be dumped via: lambda convert test.tex -f latex -t mark /tmp/test.mk
└───────────────────┘
        │
        ├────────────────────────────┐
        ▼                            ▼
┌───────────────────┐      ┌───────────────────┐
│  tex_latex_bridge │      │  tex_math_bridge  │   (for embedded math)
│  LaTeX → TexNode  │      │  Math → TexNode   │
└───────────────────┘      └───────────────────┘
        │                            │
        └────────────┬───────────────┘
                     ▼
┌─────────────────────────────────────┐
│           TexNode Tree              │   tex_node.hpp
│  (Unified typesetting node system)  │   - Char, Ligature, HList, VList
│                                     │   - Math: Fraction, Radical, Scripts
│                                     │   - Glue, Kern, Rule, Penalty
└─────────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
┌─────────────┐ ┌─────────┐ ┌─────────────┐
│ tex_hlist   │ │tex_vlist│ │tex_linebreak│   Horizontal/Vertical list building
│ HList Build │ │VList    │ │ Knuth-Plass │   Optimal line breaking algorithm
└─────────────┘ └─────────┘ └─────────────┘
                     │
                     ▼
┌─────────────────────────────────────┐
│         tex_pagebreak               │   Page breaking
│    Page Layout & Breaking           │   tex_pagebreak.hpp
└─────────────────────────────────────┘
                     │
        ┌────────────┼────────────┬────────────┐
        ▼            ▼            ▼            ▼
┌───────────┐  ┌───────────┐  ┌─────────┐  ┌─────────┐
│ tex_dvi   │  │ tex_pdf   │  │ tex_svg │  │ tex_png │
│ DVI Out   │  │ PDF Out   │  │ SVG Out │  │ PNG Out │
└───────────┘  └───────────┘  └─────────┘  └─────────┘
      │              │              │            │
      ▼              ▼              ▼            ▼
   output.dvi    output.pdf    output.svg   output.png
```

### Stage Descriptions

| Stage | File(s) | Description |
|-------|---------|-------------|
| **Parsing** | `input-latex-ts.cpp` | Tree-sitter parses LaTeX source to CST, then converted to Lambda Mark tree |
| **Bridge** | `tex_latex_bridge.cpp` | Walks Mark tree, converts LaTeX constructs to TexNode |
| **Math Bridge** | `tex_math_bridge.cpp`, `tex_math_ts.cpp` | Typesets math expressions with proper atom spacing |
| **HList** | `tex_hlist.cpp` | Builds horizontal lists (paragraph content) |
| **Line Breaking** | `tex_linebreak.cpp` | Knuth-Plass optimal line breaking |
| **VList** | `tex_vlist.cpp` | Builds vertical lists (page content) |
| **Page Breaking** | `tex_pagebreak.cpp` | Optimal page breaking |
| **DVI Output** | `tex_dvi_out.cpp` | Generates DVI files (TeX standard format) |
| **PDF Output** | `tex_pdf_out.cpp` | Direct PDF generation |
| **SVG Output** | `tex_svg_out.cpp` | Vector graphics for web display |
| **PNG Output** | `tex_png_out.cpp` | Raster image output |

### TexNode System

The unified node system (`tex_node.hpp`) represents all typeset content:

```cpp
enum class NodeClass : uint8_t {
    // Character nodes
    Char, Ligature,
    // List nodes (containers)
    HList, VList,
    // Box nodes
    HBox, VBox, VTop,
    // Spacing nodes
    Glue, Kern, Penalty,
    // Rule nodes
    Rule,
    // Math nodes
    MathList, MathChar, MathOp, Fraction, Radical, Delimiter, Accent, Scripts,
    // Structure nodes
    Paragraph, Page,
    // Special nodes
    Mark, Insert, Adjust, Whatsit, Disc,
};
```

Key design principle: **TexNode IS the view tree** - no separate conversion step between typesetting and rendering.

---

## Testing Against DVI Reference

### Reference DVI Generation

Reference DVI files are generated using standard TeX (e.g., `latex` command) and stored in `test/latex/reference/`:

```
test/latex/reference/
├── test_simple.dvi
├── test_fraction.dvi
├── test_matrix.dvi
├── test_greek.dvi
├── test_calculus.dvi
└── ... (many more test cases)
```

### Comparison Approach

The test framework (`test_latex_dvi_compare_gtest.cpp`) compares Lambda's DVI output against reference:

1. **Generate Lambda DVI**: `./lambda.exe render test.tex -o /tmp/test.dvi`
2. **Parse both DVIs**: Using `dvi_parser.hpp` to extract glyph positions
3. **Normalize for comparison**: Ignore tool-specific differences (comments, timestamps)
4. **Compare semantically**: Match character sequences and font usage

```bash
# Run DVI comparison test
./test/latex/test_dvi_compare.sh test/latex/test_simple.tex

# Or run all DVI comparison tests
./test/test_latex_dvi_compare_gtest.exe
```

### DVI Comparison Script

`test/latex/test_dvi_compare.sh` provides quick comparison:
```bash
#!/bin/bash
# Generate DVI with Lambda
./lambda.exe render "$TEX_FILE" -o "$OUT_DVI"

# Compare using dvitype (if available)
dvitype "$REF_DVI" > /tmp/ref_dvitype.txt
dvitype "$OUT_DVI" > /tmp/out_dvitype.txt

# Compare glyph sequences
diff /tmp/ref_chars.txt /tmp/out_chars.txt
```

---

## Debugging

### Dumping Intermediate AST

Use the Lambda CLI to dump the parsed AST for debugging:

```bash
# Convert LaTeX to Mark format (Lambda's AST representation)
./lambda.exe convert test.tex -f latex -t mark /tmp/test.mk

# Example output structure:
# (document
#   (paragraph
#     (text "Hello ")
#     (command (command_name "textbf") (curly_group (text "world")))
#     (text "!")))
```

### Debugging Commands

```bash
# Parse and dump AST
./lambda.exe convert input.tex -f latex -t mark output.mk

# Generate DVI with verbose output
./lambda.exe render input.tex -o output.dvi 2>&1 | tee render.log

# Compare DVI files
./test/latex/test_dvi_compare.sh test/latex/test_simple.tex

# Run specific DVI comparison test
./test/test_latex_dvi_compare_gtest.exe --gtest_filter="*Simple*"
```

### Logging

Use structured logging (outputs to `./log.txt`):
```cpp
log_debug("typeset math: %s", latex_str);
log_info("generated page %d: %d glyphs", page_num, glyph_count);
log_error("failed to load TFM font: %s", font_name);
```

---

## Test Failure Analysis and Fix Proposals

### Summary of Test Results

**Passed (12 tests)**: Basic cases work correctly
- `SimpleText`, `SimpleMath`, `Fraction`, `Greek`, `Sqrt`, `SubscriptSuperscript`, `Delimiters`, `SumIntegral`, etc.

**Failed (20 tests)**: More complex constructs

### Root Causes

#### 1. **Matrix/Array Delimiter Rendering** (Matrix, LinearAlgebra tests)

**Issue**: Lambda renders delimiter characters directly (e.g., `(`, `[`, `|`), while TeX reference DVI omits them from text comparison because they're rendered using special cmex10 extensible characters (codepoints 0, 1, etc.).

**Example** - `test_matrix.tex`:
```
Reference: "abcd1001detabcd"        (no parentheses - using extensible delimiters)
Output:    "(abcd)[1001]det|abcd|"  (ASCII delimiters)
```

**Root Cause**: The test's `extract_page_text()` function only extracts printable ASCII (32-126), so extensible delimiter characters from cmex10 (codepoints 0-31) are filtered out in the reference but Lambda outputs ASCII delimiters instead.

**Fix Approach**:
1. **Option A** (Test fix): Modify comparison to normalize delimiters - treat both extensible and ASCII delimiters as equivalent
2. **Option B** (Engine fix): Use cmex10 extensible delimiters for `\left`/`\right` in matrices
3. **Recommended**: Both - fix test comparison for robustness, and improve delimiter handling

#### 2. **Page Breaking Differences** (FontStyles, AlignmentAdvanced)

**Issue**: Reference has 2 pages, Lambda outputs 1 page.

**Example**: `AlignmentAdvanced` - "Page count mismatch: reference=2, output=1"

**Root Cause**: Lambda's page breaking algorithm may have different thresholds or the content dimensions differ.

**Fix Approach**:
- Review `tex_pagebreak.cpp` page breaking parameters
- Check if vertical spacing matches TeX reference
- Verify page dimensions are set correctly

#### 3. **Table/Tabular Environment** (Tables test)

**Issue**: Tabular content includes column specifiers and structural characters.

**Output comparison**:
```
Reference: "xx2x3111248392741664..." (content only)
Output:    "|c|c|c|xx2x3..."        (includes column spec)
```

**Root Cause**: The tabular parser is outputting the column specifier (`|c|c|c|`) as text instead of using it to configure table layout.

**Fix Approach**:
- `tex_latex_bridge.cpp`: Parse `\begin{tabular}{|c|c|c|}` column spec correctly
- Don't render column spec as text; use it to configure borders/alignment

#### 4. **Special Symbol Rendering** (Chemistry, AllOperators, AllGreek)

**Issue**: Some special symbols render differently.

**Example** from Chemistry:
```
Reference: "[C]c[D]d[A]a[B]b"   (concentration brackets)
Output:    "CcDdAaBb"          (missing brackets)
```

**Root Cause**: Subscript/superscript brackets or special formatting lost during conversion.

**Fix Approach**:
- Review `tex_math_bridge.cpp` handling of bracketed subscripts
- Ensure `[...]` in math is preserved

#### 5. **Text Mode vs Math Mode Rendering** (Various)

**Issue**: Some characters render differently based on mode context.

**Example**: `ln|x|` vs `n|x|` - logarithm function name handling.

**Fix Approach**:
- Review `\ln`, `\log`, etc. function handling in `tex_math_ts.cpp`
- Ensure function names use roman (cmr) font

### Priority Fix Order

| Priority | Issue | Files to Modify | Impact |
|----------|-------|-----------------|--------|
| 1 | Test comparison normalization | `test_latex_dvi_compare_gtest.cpp` | Fixes false negatives |
| 2 | Tabular column spec parsing | `tex_latex_bridge.cpp` | Tables test |
| 3 | Extensible delimiters | `tex_math_bridge.cpp` | Matrix tests |
| 4 | Page breaking params | `tex_pagebreak.cpp` | Multi-page tests |
| 5 | Function name rendering | `tex_math_ts.cpp` | Chemistry test |

### Immediate Test Fix

Modify `extract_page_text()` in `test_latex_dvi_compare_gtest.cpp` to normalize comparison:

```cpp
// Current: Only extracts printable ASCII
if (cp >= 32 && cp < 127) {
    text[text_len++] = (char)cp;
}

// Proposed: Map extensible delimiter codepoints to ASCII equivalents
static int32_t normalize_codepoint(int32_t cp, const char* font_name) {
    // Skip cmex10 extensible delimiter pieces (non-semantic)
    if (strcmp(font_name, "cmex10") == 0) {
        return -1;  // Skip extensible pieces
    }
    // Map common delimiter codepoints
    // ...
    return cp;
}
```

### Implemented Fix: Matrix Extensible Delimiters (cmex10)

The Matrix test was failing because delimiters were rendered using ASCII characters instead of cmex10 extensible glyphs. Fixed in `tex_math_ts.cpp`:

**Before (incorrect):**
```cpp
// Using ASCII characters - wrong!
int left_code = (left_delim == '(') ? '(' : (left_delim == '[') ? '[' : '|';
```

**After (correct):**
```cpp
// Use cmex10 extensible character codepoints per TeX standard
FontMetrics* ext_font = ctx.fonts->get_math_extension_font(ctx.base_size_pt);
// cmex10 codepoints: 0,1=parens, 2,3=brackets, 4,5=floor, 6,7=ceil, 12=vert bar
switch (left_delim) {
    case '(': left_code = 0; break;   // cmex10 left paren
    case ')': left_code = 1; break;   // cmex10 right paren
    case '[': left_code = 2; break;   // cmex10 left bracket
    case ']': left_code = 3; break;   // cmex10 right bracket
    case '|': left_code = 12; break;  // cmex10 vertical bar
    default: left_code = 0; break;
}
```

### Implemented: Tabular Environment Support

Basic `tabular` environment support added to `tex_latex_bridge.cpp`:

```cpp
// Column spec parsing: |c|c|r|l|
TabularColSpec spec = parse_tabular_colspec(col_spec, arena);

// Content collection using TabularItem structs
// - TABULAR_CONTENT: Regular cell content (text, inline_math)
// - TABULAR_ROW_SEP: Row separator (\\, linebreak_command)  
// - TABULAR_HLINE: Horizontal rule

// Row building with convert_inline_item() for proper math handling
```

### Current Test Status

| Status | Tests |
|--------|-------|
| **PASSING (13)** | NormalizationIgnoresComment, ExtractTextContent, SimpleText, SimpleMath, Fraction, Greek, Sqrt, SubscriptSuperscript, Delimiters, SumIntegral, **Matrix**, ComplexFormula, SelfConsistency |
| **FAILING (19)** | Calculus, LinearAlgebra, Physics, NumberTheory, Probability, SetTheory, Combinatorics, AbstractAlgebra, DifferentialEquations, ComplexAnalysis, Topology, NestedStructures, EdgeCases, AllGreek, AllOperators, AlignmentAdvanced, Chemistry, FontStyles, Tables |

### Remaining Issues

| Test | Root Cause | Fix Needed |
|------|------------|------------|
| Tables | Missing `&` cell separator handling | Parse cells properly between `&` markers |
| FontStyles | Page break threshold | Adjust `max_page_height` in tex_pagebreak.cpp |
| Chemistry | Bracket subscript handling | Fix `[...]` parsing in subscripts |
| AllGreek | Missing symbol mappings | Add more Greek letter variants |
| AllOperators | Missing operator mappings | Complete operator symbol table |
| AlignmentAdvanced | Multi-line alignment | Handle `align`, `gather` environments |

---

## Key Source Files

| File | Purpose |
|------|---------|
| `lambda/tree-sitter-latex/grammar.js` | Tree-sitter LaTeX grammar |
| `lambda/tree-sitter-latex-math/grammar.js` | Tree-sitter LaTeX math grammar |
| `lambda/input/input-latex-ts.cpp` | LaTeX parser (CST → Mark tree) |
| `lambda/tex/tex_node.hpp` | Unified TexNode system |
| `lambda/tex/tex_latex_bridge.cpp` | LaTeX → TexNode conversion |
| `lambda/tex/tex_math_bridge.cpp` | Math → TexNode conversion |
| `lambda/tex/tex_math_ts.cpp` | Tree-sitter based math typesetter |
| `lambda/tex/tex_tfm.cpp` | TFM font metrics parser |
| `lambda/tex/tex_font_adapter.cpp` | Font provider abstraction |
| `lambda/tex/tex_linebreak.cpp` | Knuth-Plass line breaking |
| `lambda/tex/tex_pagebreak.cpp` | Page breaking algorithm |
| `lambda/tex/tex_dvi_out.cpp` | DVI output generation |
| `lambda/tex/tex_pdf_out.cpp` | PDF output generation |
| `lambda/tex/tex_svg_out.cpp` | SVG output generation |
| `lambda/tex/dvi_parser.cpp` | DVI parser (for testing) |
| `test/test_latex_dvi_compare_gtest.cpp` | DVI comparison tests |

---

## Coordinate System

All TexNode dimensions use **CSS pixels** (96 dpi reference) for Radiant integration:

```cpp
// Conversion factors (from tex_glue.hpp):
// 1 inch = 96 CSS pixels = 72.27 TeX points = 72 PostScript points
// 1 TeX point = 96/72.27 ≈ 1.3283 CSS pixels
// 1 scaled point (sp) = 1/65536 TeX points

// For DVI output, convert CSS px → scaled points:
int32_t sp = px_to_sp(px);  // px * (72.27/96) * 65536
```
