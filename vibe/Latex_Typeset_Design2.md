# LaTeX Support Strategy for Lambda/Radiant

This document analyzes the path forward for building comprehensive LaTeX support in Lambda/Radiant, comparing two approaches and providing strategic recommendations.

## Executive Summary

**Recommendation: LaTeX.js-style Approach with Unified Pipeline**

A complete TeX implementation is not feasible for Lambda/Radiant. Instead, pursue a LaTeX.js-style approach that emulates LaTeX layout and typesetting as closely as possible while skipping complicated/legacy features.

**Key Strategic Goal**: Unify the existing HTML pipeline (`format_latex_html_v2.cpp`) and DVI pipeline (`tex_latex_bridge.cpp`) into a single codebase. The unified pipeline will:
- Use native TexNode-based math rendering for all outputs (no KaTeX dependency)
- Support HTML, SVG, PDF, PNG, and DVI from a single document model
- Phase out `format_latex_html_v2.cpp` after achieving test parity

---

## Option Analysis

### Option 1: LaTeX.js-style Emulation

**Approach**: Emulate TeX/LaTeX layout and typesetting semantically, handling common constructs natively without implementing the full TeX macro processor.

| Aspect | Details |
|--------|---------|
| **Development time** | 6-12 months to reach 90%+ document coverage |
| **Document coverage** | ~90% of common LaTeX documents |
| **Architecture fit** | ✅ Tree-sitter grammar works perfectly |
| **Maintenance burden** | Low-medium |
| **User expectation** | "LaTeX-like, fast, modern" |

**Advantages**:
- Already aligned with current architecture (grammar designed to "match LaTeX.js structure")
- Proven model - LaTeX.js handles vast majority of real-world documents
- Fits Lambda's value proposition: fast, cross-platform, modern tooling
- Current infrastructure (TFM fonts, Knuth-Plass line breaking, TexNode) supports this approach

### Option 2: Complete TeX Implementation

**Approach**: Implement the full TeX macro processor, catcode system, and package loading mechanism.

| Aspect | Details |
|--------|---------|
| **Development time** | 3-5+ years minimum |
| **Document coverage** | 100% (theoretically) |
| **Architecture fit** | ❌ Requires fundamental redesign |
| **Maintenance burden** | Very high |
| **User expectation** | "Drop-in replacement for pdflatex" |

**Why This Is Impractical**:

1. **TeX is Turing-complete**: The macro processor is a full programming language with:
   - Token expansion and replacement
   - Catcode system (character meaning changes dynamically)
   - Conditional execution (`\ifmmode`, `\ifnum`, etc.)
   - Loop constructs (`\loop`, `\repeat`)
   - Token registers and manipulation

2. **Tree-sitter incompatibility**: Tree-sitter assumes a static grammar. TeX processes tokens character-by-character with dynamic macro expansion. Example:
   ```latex
   \makeatletter     % Changes @ from "other" to "letter"
   \def\@foo{bar}    % Now valid - @ is part of command name
   \makeatother      % Changes @ back to "other"
   ```
   Tree-sitter cannot handle this because the grammar changes at runtime.

3. **Package ecosystem**: 6000+ CTAN packages. Even supporting the top 50 would require years of work.

4. **Counter-register system**: 256 count, dimen, skip, muskip, box, and token registers with complex interactions.

---

## Current Architecture Assessment

### What's Already Working

| Component | Status | Notes |
|-----------|--------|-------|
| **Tree-sitter LaTeX parser** | ✅ Solid | Handles document structure well |
| **Tree-sitter Math parser** | ✅ Solid | Separate grammar for math mode |
| **TFM font support** | ✅ Good | Character metrics, ligatures, kerning |
| **Knuth-Plass line breaking** | ✅ Implemented | Optimal paragraph breaking |
| **TexNode system** | ✅ Unified | Single node type for typesetting + rendering |
| **DVI output** | ✅ Working | 16/32 tests passing |
| **Basic math typesetting** | ✅ Working | Fractions, radicals, scripts, delimiters |

### What Needs Work

| Component | Status | Priority |
|-----------|--------|----------|
| **Environment handling** | 🟡 Partial | High - `align`, `gather`, `cases` |
| **Table/array layout** | 🟡 Basic | High - proper `&` parsing |
| **Symbol coverage** | 🟡 ~70% | Medium - complete tables |
| **Page breaking** | 🟡 Basic | Medium - multi-page documents |
| **Simple macros** | 🔴 Missing | Medium - `\newcommand` substitution |

---

## Proposed Roadmap

### Phase 1: Core Completion (Current → 90% DVI test pass)

**Goal**: Make the fundamentals rock-solid.

| Task | Files | Effort |
|------|-------|--------|
| Complete symbol tables | `tex_math_ts.cpp` | 1 week |
| `align`/`gather` environments | `tex_latex_bridge.cpp` | 2 weeks |
| Fix table `&` cell separation | `tex_latex_bridge.cpp` | 1 week |
| All Greek letter variants | `tex_math_ts.cpp` | 3 days |
| Remaining operators | `tex_math_ts.cpp` | 3 days |

### Phase 2: Essential Package Emulation

**Approach**: Built-in semantic recognition, not macro expansion.

```cpp
// When \usepackage{amsmath} is seen, set a flag
ctx.packages.amsmath = true;

// When processing \begin{align}, check flag and handle natively
if (ctx.packages.amsmath && tag_eq(env_name, "align")) {
    return build_align_environment(elem, ctx);
}
```

**Priority packages to emulate**:

| Package | Features | Complexity |
|---------|----------|------------|
| `amsmath` | `align`, `gather`, `cases`, `matrix` variants | Medium |
| `amssymb` | Extended math symbols | Low (already mostly done) |
| `graphicx` | `\includegraphics` | Low |
| `hyperref` | `\href`, `\url` → HTML links | Low |
| `xcolor` | Basic color support | Low |
| `geometry` | Page margins | Low |
| `tikz` | Basic subset only | High (optional) |

### Phase 3: Simple Macro Support

Support **simple** user macros only - no conditionals, no `\def`.

```latex
% Support these patterns:
\newcommand{\R}{\mathbb{R}}                    % Simple substitution
\newcommand{\norm}[1]{\|#1\|}                  % Single argument
\newcommand{\inner}[2]{\langle#1,#2\rangle}    % Multiple arguments

% Do NOT attempt to support:
\newcommand{\foo}{\ifmmode x\else y\fi}        % Conditionals
\def\macro#1{...}                              % TeX \def with delimited args
\expandafter\foo\bar                           % Token manipulation
```

**Implementation**: Store as pattern → replacement pairs, expand during AST walk (already partially implemented in `format_latex_html_v2.cpp`).

### Phase 4: Output Quality Polish

| Output | Enhancements |
|--------|--------------|
| **PDF** | Proper font embedding, hyperlinks, bookmarks |
| **SVG** | Text selection, accessibility attributes |
| **HTML** | MathML fallback for math expressions |
| **DVI** | Complete for testing against TeX reference |

---

## Architecture Recommendations

### Keep Tree-sitter for Parsing

Tree-sitter works excellently for the LaTeX.js approach because you're parsing a **fixed grammar**, not implementing TeX's dynamic expansion. The current two-grammar design (latex + latex-math) is optimal.

### Add Package Registry Layer

```cpp
// New file: tex_packages.hpp
struct PackageSupport {
    bool amsmath = false;
    bool amssymb = false;
    bool graphicx = false;
    bool hyperref = false;
    bool xcolor = false;
    
    // Package-specific state
    std::map<std::string, std::string> defined_colors;
    
    void load_package(const char* name, const char* options);
    bool is_loaded(const char* name) const;
};
```

### Unify the Two Pipelines

Currently there are two rendering paths:
1. **DVI pipeline**: `tex_latex_bridge.cpp` → `tex_math_ts.cpp` → `tex_dvi_out.cpp`
2. **HTML pipeline**: `format_latex_html_v2.cpp` → KaTeX/MathJax for math

**Recommendation**: Converge on the TexNode-based pipeline for consistency:
- HTML output can render TexNode tree directly (similar to SVG output)
- Math rendering uses same code path for all outputs
- Single source of truth for layout algorithms

**See [Pipeline Unification Strategy](#pipeline-unification-strategy) below for detailed implementation plan.**

### Document the Supported Subset

Create clear user-facing documentation:

```markdown
# Lambda LaTeX Support

## Fully Supported
- Document classes: article, report, book (basic layout)
- Math: All standard symbols, fractions, matrices, alignment environments
- Environments: equation, align, gather, itemize, enumerate, tabular, figure
- Packages: amsmath, amssymb, graphicx, hyperref, xcolor

## Partially Supported  
- tikz: Basic shapes and arrows only
- Custom macros: Simple substitution only (no conditionals)

## Not Supported
- biblatex/biber integration
- Index generation (makeindex)
- Complex TikZ diagrams
- Custom catcode manipulation
- \def with delimited parameters
```

---

## Comparison with LaTeX.js

| Feature | LaTeX.js | Lambda/Radiant |
|---------|----------|----------------|
| **Parsing** | Custom PEG.js parser | Tree-sitter (faster, incremental) |
| **Math rendering** | KaTeX | Native TexNode (pixel-perfect) |
| **Output formats** | HTML only | HTML, SVG, PDF, PNG, DVI |
| **Font handling** | Web fonts | TFM + TrueType |
| **Line breaking** | Browser CSS | Knuth-Plass optimal |
| **Page breaking** | None | Implemented |

**Lambda/Radiant advantages**:
- True typeset output (not browser-rendered)
- Multiple output formats from single pipeline
- Pixel-accurate TeX metrics via TFM
- Incremental parsing for editor integration

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| User expects full TeX compatibility | Clear documentation of supported subset |
| Edge cases in real documents | Graceful degradation with warnings |
| Performance with large documents | Incremental parsing, lazy evaluation |
| Keeping up with new packages | Focus on stable, widely-used packages |

---

## Pipeline Unification Strategy

### Current State: Two Separate Pipelines

The codebase currently has two distinct LaTeX rendering pipelines that share only the Tree-sitter parser:

| Pipeline | Entry Point | Math Handling | Output | Lines of Code |
|----------|-------------|---------------|--------|---------------|
| **HTML Pipeline** | `format_latex_html_v2.cpp` | Delegates to KaTeX/MathJax | HTML/CSS | ~10,000 |
| **DVI Pipeline** | `tex_latex_bridge.cpp` | Native `tex_math_ts.cpp` | TexNode → DVI/SVG/PDF | ~3,000 |

**Problems with dual pipelines**:
1. **Duplicated effort**: ~300 command handlers in HTML pipeline vs growing handlers in DVI pipeline
2. **Inconsistent rendering**: Math looks different between HTML (KaTeX) and PDF (native)
3. **Maintenance burden**: Bug fixes must be applied twice
4. **Feature drift**: New features added to one pipeline but not the other

### Goal: Unified Pipeline with HTML Output

**Strategy**: Evolve the DVI pipeline to support HTML output, then phase out `format_latex_html_v2.cpp`.

```
                    ┌─────────────────────────────────────────────────────┐
                    │                   UNIFIED PIPELINE                   │
                    └─────────────────────────────────────────────────────┘
                                            │
    LaTeX Source ──► Tree-sitter Parser ──► Lambda Element AST
                                            │
                                            ▼
                              ┌─────────────────────────┐
                              │   tex_latex_bridge.cpp   │
                              │   (Document Processing)  │
                              └─────────────────────────┘
                                            │
                              ┌─────────────┴─────────────┐
                              ▼                           ▼
                    ┌─────────────────┐         ┌─────────────────┐
                    │ tex_math_ts.cpp │         │ Text/Paragraph  │
                    │ (Math Typeset)  │         │   Processing    │
                    └─────────────────┘         └─────────────────┘
                              │                           │
                              └─────────────┬─────────────┘
                                            ▼
                              ┌─────────────────────────┐
                              │      TexNode Tree       │
                              │   (Unified Document)    │
                              └─────────────────────────┘
                                            │
                    ┌───────────┬───────────┼───────────┬───────────┐
                    ▼           ▼           ▼           ▼           ▼
              ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
              │   DVI   │ │   PDF   │ │   SVG   │ │   PNG   │ │  HTML   │
              │  Output │ │  Output │ │  Output │ │  Output │ │  Output │
              └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘
```

### Document Model Layer

Introduce an intermediate **Document Model** that captures both semantic structure and typeset layout hints:

```cpp
// New file: tex_document_model.hpp

struct TexDocumentModel {
    // Document metadata
    const char* document_class;      // "article", "report", etc.
    PackageSupport packages;          // Loaded packages
    
    // Document structure (semantic)
    struct Section {
        int level;                    // 0=part, 1=chapter, 2=section, etc.
        const char* title;
        ArrayList<DocElement*> content;
    };
    ArrayList<Section*> sections;
    
    // Typeset results (when computed)
    TexNode* typeset_root;           // Full typeset tree (optional)
    bool is_typeset;                 // Whether layout has been computed
    
    // Build from Lambda Element AST
    static TexDocumentModel* from_element(Item elem, TexContext& ctx);
    
    // Output renderers
    void to_html(StrBuf* out, const HtmlOptions& opts);
    void to_texnode(TexNode** out, const TypesetOptions& opts);
};

struct DocElement {
    enum Type { PARAGRAPH, MATH_INLINE, MATH_DISPLAY, LIST, TABLE, FIGURE, ENVIRONMENT };
    Type type;
    
    // For MATH_INLINE/MATH_DISPLAY: already typeset via tex_math_ts.cpp
    TexNode* math_node;
    
    // For PARAGRAPH: raw text + inline math spans
    struct TextRun {
        const char* text;
        TexNode* inline_math;        // if this run is inline math
    };
    ArrayList<TextRun> runs;
    
    // For LIST, TABLE, ENVIRONMENT: children
    ArrayList<DocElement*> children;
};
```

### Key Design Principles

#### 1. Math Always Uses TexNode

Both HTML and DVI output render math identically via `tex_math_ts.cpp`:

```cpp
// In unified pipeline - math is always typeset
TexNode* math = build_math_expression(math_elem, ctx);

// HTML output: render TexNode to SVG, embed inline
void render_math_to_html(TexNode* math, StrBuf* out) {
    StrBuf svg;
    render_texnode_to_svg(math, &svg);
    strbuf_append(out, "<span class=\"math\">");
    strbuf_append_buf(out, &svg);
    strbuf_append(out, "</span>");
}

// DVI output: emit TexNode directly
void render_math_to_dvi(TexNode* math, DviWriter* dvi) {
    emit_texnode(math, dvi);
}
```

**Benefits**:
- Identical math rendering across all outputs
- No dependency on KaTeX/MathJax
- Pixel-perfect TeX math in HTML

#### 2. Text Layout: Two Modes

Support both **fast HTML** and **accurate typeset** modes:

| Mode | Line Breaking | Font Metrics | Use Case |
|------|---------------|--------------|----------|
| **Fast HTML** | Browser CSS | Approximate | Quick preview, web display |
| **Typeset** | Knuth-Plass | TFM exact | PDF, print, high-quality |

```cpp
struct HtmlOptions {
    bool use_typeset_paragraphs;    // false = let browser wrap
    bool embed_fonts;               // true = include WOFF2 fonts
    const char* math_render;        // "svg" (default) or "mathml"
};
```

#### 3. Command Handler Unification

Migrate command handlers from `format_latex_html_v2.cpp` to `tex_latex_bridge.cpp`:

```cpp
// Current HTML pipeline (to be deprecated):
// format_latex_html_v2.cpp
void LatexProcessor::handle_textbf(Element* elem) {
    output("<strong>");
    process_children(elem);
    output("</strong>");
}

// Unified pipeline:
// tex_latex_bridge.cpp
DocElement* handle_textbf(Element* elem, TexContext& ctx) {
    DocElement* result = alloc_doc_element(DocElement::STYLED_TEXT);
    result->style = STYLE_BOLD;
    result->children = process_children(elem, ctx);
    return result;
}

// HTML renderer interprets DocElement:
void doc_to_html(DocElement* elem, StrBuf* out) {
    if (elem->style == STYLE_BOLD) {
        strbuf_append(out, "<strong>");
        for (auto child : elem->children) doc_to_html(child, out);
        strbuf_append(out, "</strong>");
    }
}
```

### Migration Plan

#### Step 1: Parallel Testing Infrastructure

Ensure the unified pipeline can pass existing HTML tests before deprecating the old pipeline.

```bash
# New test target
make test-latex-html-unified    # Run HTML tests against unified pipeline

# Compare outputs
./lambda.exe convert doc.tex -t html --pipeline=legacy  > legacy.html
./lambda.exe convert doc.tex -t html --pipeline=unified > unified.html
diff legacy.html unified.html
```

#### Step 2: Incremental Command Migration

Migrate commands in priority order:

| Priority | Commands | Complexity |
|----------|----------|------------|
| **P0** | `\textbf`, `\textit`, `\emph`, `\underline` | Low |
| **P0** | `\section`, `\subsection`, `\paragraph` | Low |
| **P1** | `\begin{itemize}`, `\begin{enumerate}` | Medium |
| **P1** | `\begin{tabular}`, `\begin{table}` | Medium |
| **P2** | `\includegraphics`, `\href`, `\url` | Low |
| **P2** | `\begin{figure}`, `\caption` | Medium |
| **P3** | `\newcommand` macro expansion | Medium |
| **P3** | Package-specific commands | Varies |

#### Step 3: Feature Parity Validation

Track feature parity between pipelines:

```cpp
// Test file: test/latex/html_parity_test.cpp
TEST(HtmlParity, TextFormatting) {
    auto legacy = render_html_legacy("\\textbf{bold} \\textit{italic}");
    auto unified = render_html_unified("\\textbf{bold} \\textit{italic}");
    EXPECT_EQ(normalize_html(legacy), normalize_html(unified));
}

TEST(HtmlParity, MathInline) {
    // Math should be identical (both use tex_math_ts.cpp in unified)
    auto unified = render_html_unified("Equation $x^2 + y^2 = z^2$ here.");
    EXPECT_CONTAINS(unified, "<svg");  // Math rendered as SVG
}
```

#### Step 4: Deprecation and Removal

```cpp
// In format_latex_html_v2.cpp - add deprecation notice
[[deprecated("Use unified pipeline via tex_latex_bridge.cpp")]]
void format_latex_to_html_v2(Item input, StrBuf* output);

// Eventually remove after all tests pass on unified pipeline
```

### HTML Output Specifics

#### CSS Generation

The unified pipeline generates semantic CSS classes:

```html
<!-- Unified pipeline HTML output -->
<article class="latex-document latex-article">
  <section class="latex-section">
    <h2 class="latex-section-title">Introduction</h2>
    <p class="latex-paragraph">
      Consider the equation 
      <span class="latex-math latex-math-inline">
        <svg>...</svg>  <!-- TexNode rendered to SVG -->
      </span>
      where...
    </p>
  </section>
</article>

<style>
.latex-document { max-width: 800px; margin: 0 auto; font-family: 'Computer Modern', serif; }
.latex-section-title { font-size: 1.4em; margin-top: 1.5em; }
.latex-math-inline svg { vertical-align: middle; }
.latex-math-display { text-align: center; margin: 1em 0; }
</style>
```

#### Font Embedding Options

```cpp
struct HtmlOptions {
    enum FontMode {
        FONT_SYSTEM,      // Use system fonts (fast, less accurate)
        FONT_WEBFONT,     // Link to Google Fonts / CDN
        FONT_EMBEDDED,    // Embed WOFF2 in HTML (self-contained)
    };
    FontMode font_mode = FONT_WEBFONT;
};
```

### Success Criteria for Unification

| Criterion | Target | Validation |
|-----------|--------|------------|
| HTML test pass rate | 100% of existing tests | `make test-latex-html-unified` |
| Math rendering parity | Pixel-identical SVG | Visual diff tests |
| Performance | ≤ 2x legacy pipeline | Benchmark suite |
| File size | ≤ 1.5x legacy output | Size comparison |
| Feature coverage | All P0-P2 commands | Command checklist |

### Timeline

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| **Phase A**: Document Model design | 1 week | `tex_document_model.hpp` spec |
| **Phase B**: Math SVG embedding | 2 weeks | Math renders identically in HTML |
| **Phase C**: P0 command migration | 2 weeks | Basic text formatting works |
| **Phase D**: P1 command migration | 3 weeks | Lists, tables, environments |
| **Phase E**: P2 command migration | 2 weeks | Images, links, figures |
| **Phase F**: Test parity | 2 weeks | 100% HTML test pass rate |
| **Phase G**: Deprecation | 1 week | Remove `format_latex_html_v2.cpp` |

**Total: ~13 weeks to full unification**

---

## Success Metrics

| Metric | Target |
|--------|--------|
| DVI comparison tests passing | 90%+ (currently 50%) |
| HTML test pass rate (unified) | 100% (parity with legacy) |
| Common document coverage | 90% of arxiv papers render correctly |
| Rendering speed | < 100ms for typical paper |
| Output quality | Indistinguishable from pdflatex at 300dpi |

---

## Conclusion

The LaTeX.js-style approach is the clear choice for Lambda/Radiant:

1. **Feasible timeline**: 6-12 months vs 3-5+ years
2. **Architectural alignment**: Current Tree-sitter + TexNode design supports this perfectly
3. **User value**: Fast, cross-platform, modern LaTeX rendering for 90% of use cases
4. **Pragmatic scope**: Users with exotic requirements can export to `.tex` and use pdflatex

The goal is not to replace TeX, but to provide a **fast, modern, integrated LaTeX experience** for common documents while acknowledging that edge cases may require traditional TeX tooling.
