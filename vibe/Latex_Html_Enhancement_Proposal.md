# LaTeX to HTML Conversion Enhancement Proposal

**Date:** January 19, 2026  
**Status:** Draft Proposal  
**Based on:** Test analysis of 169 tests (65 passing, 104 failing)

---

## Executive Summary

The current LaTeX-to-HTML pipeline passes **59 baseline tests** (100%) and **6 extended tests**. To reach comprehensive coverage, structural enhancements are needed in three main areas:

1. **Counter System** - Implement full TeX counter arithmetic and formatting
2. **Math-to-MathML/HTML** - Improve math environment handling and MathML output
3. **Advanced Macro System** - Support optional arguments and complex package macros

---

## Current Architecture Analysis

### Pipeline Flow
```
LaTeX Source
     ↓
Tree-sitter Parser (input-latex-ts.cpp)
     ↓
Lambda Element AST (Item tree)
     ↓
TexDocumentModel (tex_document_model.cpp)
     ├── DocElement tree (semantic structure)
     ↓
doc_model_to_html() → HTML output
```

### Key Code Locations

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| Parser | `input-latex-ts.cpp` | ~2K | Tree-sitter LaTeX → Lambda AST |
| Model Builder | `tex_document_model.cpp` | ~8.5K | AST → DocElement tree |
| HTML Renderer | `tex_document_model.cpp` | L1800-2000 | DocElement → HTML |
| Math Bridge | `tex_math_bridge.cpp` | ~2K | Math → TexNode (for DVI) |

### Current Test Status

| Category | Passing | Failing | Gap Analysis |
|----------|---------|---------|--------------|
| **Basic Text/Formatting** | 31 | 0 | ✅ Complete |
| **Lists/Environments** | 13 | 0 | ✅ Complete |
| **Whitespace** | 12 | 0 | ✅ Complete |
| **Fonts (basic)** | 3 | 22 | Missing: font families, em-lengths |
| **Counters** | 0 | 2 | Missing: counter system |
| **Macros (optional args)** | 2 | 4 | Missing: optional argument parsing |
| **Sectioning** | 0 | 3 | Heading format differs (h2 vs h1) |
| **Spacing** | 0 | 4 | Missing: hspace, vspace |
| **Label/Ref** | 0 | 7 | Missing: \ref resolution |
| **Boxes** | 0 | 4 | Missing: \parbox, \mbox |
| **AMS Math** | 0 | 7 | Missing: equation environments |
| **Structure** | 0 | 15 | Various: sections, figures, etc. |

---

## Proposed Structural Enhancements

### Enhancement 1: Counter System Implementation

**Priority:** High  
**Effort:** 3-4 days  
**Impact:** Enables counters, sectioning numbers, reference resolution

#### Current Gap
Counter commands (`\newcounter`, `\setcounter`, `\stepcounter`, `\arabic`, `\roman`, `\Roman`, `\fnsymbol`) are parsed but produce literal text like `"c"` instead of evaluating counter values.

#### Proposed Architecture

```cpp
// Add to tex_document_model.hpp
struct TeXCounter {
    const char* name;
    int value;
    const char* parent;        // Counter to reset on (e.g., section resets subsection)
    ArrayList* children;       // Counters that reset when this one steps
};

struct CounterManager {
    Arena* arena;
    TeXCounter* counters;      // Dynamically sized array
    int count;
    int capacity;
    
    // API
    void define(const char* name, const char* parent = nullptr);
    void set(const char* name, int value);
    void step(const char* name);  // Increment and reset children
    void add(const char* name, int delta);
    int get(const char* name);
    
    // Formatting
    const char* format_arabic(const char* name, Arena* arena);
    const char* format_roman(const char* name, Arena* arena);
    const char* format_Roman(const char* name, Arena* arena);
    const char* format_alph(const char* name, Arena* arena);
    const char* format_Alph(const char* name, Arena* arena);
    const char* format_fnsymbol(const char* name, Arena* arena);
};
```

#### Implementation Tasks

1. **Define CounterManager** in `tex_document_model.hpp`
2. **Add counter field** to `TexDocumentModel`
3. **Handle counter commands** in `build_doc_element()`:
   - `\newcounter{name}` → `counters.define(name)`
   - `\setcounter{name}{value}` → evaluate expression → `counters.set()`
   - `\stepcounter{name}` → `counters.step()`
   - `\addtocounter{name}{value}` → evaluate → `counters.add()`
4. **Handle format commands**:
   - `\arabic{counter}` → `counters.format_arabic()`
   - `\roman{counter}` → `counters.format_roman()`
   - etc.
5. **Expression evaluator** for `\value{counter}` in arithmetic contexts

#### Test Fixtures
- `latexjs/counters/01_counters.tex`
- `latexjs/counters/02_clear_inner_counters.tex`

---

### Enhancement 2: Math to HTML/MathML Unification

**Priority:** High  
**Effort:** 5-7 days  
**Impact:** Enables all AMS math environments in HTML output

#### Current Gap
Math environments like `equation`, `align`, `gather`, `split`, `matrix` variants are not properly handled. Currently:
- Display math `\[ ... \]` produces fallback HTML: `<div class="math-display"><span class="math-fallback">\[...\]</span></div>`
- Matrix environments break at line breaks
- MathML output is not generated

#### Proposed Architecture

Option A: **MathML Generation** (Preferred for accessibility)
```cpp
// New file: tex_math_html.hpp / tex_math_html.cpp

namespace tex {

// Math to MathML converter
struct MathToMathML {
    Arena* arena;
    StrBuf* output;
    
    // Entry points
    void convert_inline(const ItemReader& math_content);
    void convert_display(const ItemReader& math_content);
    
    // Core conversions
    void emit_element(const ItemReader& elem);
    void emit_frac(const ItemReader& numerator, const ItemReader& denominator);
    void emit_sqrt(const ItemReader& radicand, const ItemReader* index);
    void emit_matrix(const ItemReader& env, const char* delim_left, const char* delim_right);
    void emit_align(const ItemReader& env, bool numbered);
};

// Called from doc_element_to_html for MATH_INLINE, MATH_DISPLAY, etc.
void math_to_mathml(DocElement* math_elem, StrBuf* output, Arena* arena);

}
```

Option B: **Use Existing tex_math_bridge.cpp for TexNode, then SVG**
- Reuse DVI pipeline's math typesetting
- Render inline SVG for browser display
- Less accessible but higher visual fidelity

#### Recommended Approach: Hybrid
1. Generate MathML for **simple** math (no special typesetting needed)
2. Use inline SVG for **complex** math (matrices, aligned equations)
3. Always include LaTeX source as `alttext` attribute for fallback

#### Implementation Tasks

1. **Create `tex_math_html.cpp`** with MathML generation
2. **Handle math environments**:
   - `equation*` / `equation` (with numbering)
   - `align*` / `align`
   - `gather*` / `gather`
   - `split`, `multline`
   - `matrix`, `pmatrix`, `bmatrix`, etc.
3. **Integrate with `doc_element_to_html()`**:
   - `MATH_INLINE` → call `math_to_mathml()` or inline SVG
   - `MATH_DISPLAY` → same, wrapped in block container
4. **Equation numbering** integration with counter system

#### Test Fixtures
- `ams/amsdisplay.tex`, `ams/matrix.tex`
- All `math/*.tex` fixtures

---

### Enhancement 3: Advanced Macro System

**Priority:** Medium-High  
**Effort:** 3-4 days  
**Impact:** Enables optional argument macros, common package support

#### Current Gap
The current macro system handles `\newcommand{name}[n]{replacement}` with mandatory arguments only. Optional arguments `[default]` are not supported:
- `\newcommand{\cmd}[2][default]{#1 and #2}` fails
- Package-defined commands with optional args fail

#### Proposed Architecture

```cpp
// Enhanced MacroDef in tex_document_model.hpp
struct MacroDef {
    const char* name;
    int num_args;                  // Total arguments (including optional)
    int num_optional;              // Number of optional args (0 or 1 in LaTeX)
    const char* replacement;       // Body with #1, #2, etc.
    const char* default_values[9]; // Default values for optional args
    bool star_variant;             // Has \cmd* variant
};

// Macro expansion context
struct MacroExpansion {
    const MacroDef* def;
    const char* args[9];           // Collected argument values
    bool star_used;
    
    const char* expand(Arena* arena);
};
```

#### Implementation Tasks

1. **Enhance `\newcommand` parsing** to detect optional arg syntax:
   - `\newcommand{\cmd}[2][default]{body}` → `num_optional=1, num_args=2`
2. **Update `try_expand_macro()`** to:
   - Check for optional argument `[...]` first
   - Use default value if not provided
   - Then consume mandatory arguments
3. **Handle `\providecommand`** (don't override existing)
4. **Handle `\renewcommand`** (must exist)
5. **Package stubs** for common packages (echo, etc.)

#### Test Fixtures
- `latexjs/macros/03_custom_macros_with_an_optional_argument.tex`
- `latexjs/macros/04_custom_macros_with_optional_and_mandator.tex`
- `latexjs/macros/05_brackets_can_be_used_in_optional_argumen.tex`
- `latexjs/macros/06_custom_macros_with_optional_and_mandator.tex`

---

### Enhancement 4: Heading Format Alignment

**Priority:** Medium  
**Effort:** 1-2 days  
**Impact:** Fixes sectioning tests

#### Current Gap
Chapter headings use `<h2>` with `<span class="section-number">` format instead of expected `<h1>` with `<div>` for chapter prefix.

**Expected:**
```html
<h1 id="sec-1"><div>Chapter 1</div>A Chapter</h1>
```

**Actual:**
```html
<h2 id="sec-1" class="heading-1"><span class="section-number">1</span>A Chapter</h2>
```

#### Implementation Tasks

1. **Update `render_heading_html()`** to:
   - Use `<h1>` for chapters (level 1)
   - Output "Chapter N" prefix as `<div>` for chapters
   - Keep current format for sections/subsections
2. **Make format configurable** via `HtmlOutputOptions`:
   - `heading_format: HYBRID` (latex.js style with `<div>`)
   - `heading_format: MODERN` (semantic with `<span class="number">`)

---

### Enhancement 5: Spacing Commands

**Priority:** Medium  
**Effort:** 2 days  
**Impact:** Fixes spacing tests

#### Current Gap
Horizontal and vertical spacing commands need implementation:
- `\hspace{length}` → `<span style="margin-left: ...">` or CSS
- `\vspace{length}` → `<div style="margin-top: ...">` or `<br>` + margin
- `\quad`, `\qquad`, `\enspace` → appropriate Unicode spaces or spans
- `\negthinspace`, `\thinspace` → thin space Unicode

#### Implementation Tasks

1. **Add spacing command handling** in `build_inline_content()`:
   - Parse length values (cm, em, pt, etc.)
   - Convert to CSS units
2. **Create DocElemType::HSPACE** and **VSPACE** variants
3. **Render as CSS** in `doc_element_to_html()`

---

### Enhancement 6: Cross-Reference Resolution

**Priority:** Medium  
**Effort:** 2-3 days  
**Impact:** Fixes label/ref tests

#### Current Gap
`\label{key}` and `\ref{key}` need two-pass resolution:
1. First pass: collect all labels with their context (section number, equation number)
2. Second pass: resolve `\ref{key}` to appropriate anchor and text

#### Implementation (Partially Done)
The `TexDocumentModel` already has `labels` array and `resolve_pending_refs()`. Need to:

1. **Ensure labels capture context** at definition point
2. **Verify `\ref` generates working links**
3. **Support `\pageref`** (use same anchor in HTML, since no pages)

---

### Enhancement 7: Box Commands

**Priority:** Low-Medium  
**Effort:** 2 days  
**Impact:** Fixes box tests

#### Commands to Support
- `\mbox{content}` → inline box (prevents line break)
- `\makebox[width][pos]{content}` → sized inline box
- `\parbox[pos][height][inner-pos]{width}{content}` → paragraph box
- `\fbox{content}` → framed box

#### HTML Mapping
```html
\mbox{text}  →  <span class="mbox">text</span>
\parbox[t]{5cm}{text}  →  <div class="parbox" style="width:5cm; vertical-align:top">text</div>
\fbox{text}  →  <span class="fbox">text</span>
```

---

## Implementation Roadmap

### Phase 1: Core Infrastructure (Week 1)
| Task | Files | Days |
|------|-------|------|
| Counter system | `tex_document_model.hpp/cpp` | 3 |
| Heading format fix | `tex_document_model.cpp` | 1 |
| **Phase 1 Target:** +12 tests passing | | |

### Phase 2: Math & Macros (Week 2)
| Task | Files | Days |
|------|-------|------|
| Math to MathML | `tex_math_html.cpp` (new) | 5 |
| Optional arg macros | `tex_document_model.cpp` | 2 |
| **Phase 2 Target:** +20 tests passing | | |

### Phase 3: Refinements (Week 3)
| Task | Files | Days |
|------|-------|------|
| Spacing commands | `tex_document_model.cpp` | 2 |
| Cross-reference fixes | `tex_document_model.cpp` | 2 |
| Box commands | `tex_document_model.cpp` | 2 |
| **Phase 3 Target:** +15 tests passing | | |

### Phase 4: Package Support (Week 4)
| Task | Files | Days |
|------|-------|------|
| Font package stubs | `tex/packages/` | 2 |
| AMS package stubs | `tex/packages/` | 2 |
| Integration testing | - | 2 |
| **Phase 4 Target:** +20 tests passing | | |

---

## Architecture Decision: MathML vs SVG

### Option A: MathML Output (Recommended)

**Pros:**
- Native browser support (Chrome, Firefox, Safari)
- Accessible (screen readers)
- Searchable, copyable text
- Smaller output size

**Cons:**
- Some rendering differences from TeX
- Complex environments need careful handling

### Option B: Inline SVG

**Pros:**
- Pixel-perfect TeX rendering
- Reuses existing DVI pipeline

**Cons:**
- Not accessible
- Not searchable
- Larger output size
- Slower rendering

### Recommendation: Hybrid
- Default to MathML for simple expressions
- Use SVG for complex environments (align, matrices) when high fidelity needed
- Provide option: `HtmlOutputOptions::math_mode = {MATHML, SVG, AUTO}`

---

## Files to Create/Modify

### New Files
| File | Purpose |
|------|---------|
| `lambda/tex/tex_math_html.hpp` | Math to MathML converter header |
| `lambda/tex/tex_math_html.cpp` | Math to MathML converter implementation |
| `lambda/tex/tex_counter.hpp` | Counter system (if separating from model) |

### Modified Files
| File | Changes |
|------|---------|
| `lambda/tex/tex_document_model.hpp` | CounterManager, enhanced MacroDef |
| `lambda/tex/tex_document_model.cpp` | Counter handling, macro optargs, heading format |
| `build_lambda_config.json` | Add new source files |

---

## Success Metrics

| Milestone | Tests Passing | Date |
|-----------|---------------|------|
| Baseline (current) | 65/169 (38%) | Now |
| Phase 1 Complete | 77/169 (46%) | +1 week |
| Phase 2 Complete | 97/169 (57%) | +2 weeks |
| Phase 3 Complete | 112/169 (66%) | +3 weeks |
| Phase 4 Complete | 132/169 (78%) | +4 weeks |
| Full Coverage | 160/169 (95%) | +6 weeks |

---

## Appendix: Test Failure Categories

### A. Immediate Wins (Low Effort, High Impact)

1. **Hyphen vs Unicode hyphen** in macro output
   - Test: `latexjs/macros/03_*` outputs `‐` instead of `-`
   - Fix: Check text transformation in `transform_text_ligatures()`

2. **Chapter heading format**
   - Tests: `latexjs/sectioning/*`
   - Fix: Update `render_heading_html()` format

### B. Medium Effort

1. **Counter arithmetic** - requires expression evaluator
2. **Optional arguments** - requires parser enhancement
3. **Spacing commands** - straightforward HTML mapping

### C. Larger Efforts

1. **Full MathML generation** - comprehensive math coverage
2. **AMS environments** - equation numbering, alignment
3. **Font packages** - multiple font families and encodings
