# Lambda LaTeX Package Proposal

> **Location:** `lambda/package/latex/`  
> **References:** Math package (`lambda/package/math/`), LaTeX.js (`ref/latex-js/`), LaTeXML (`ref/latexml/`), C++ implementation (`lambda/input/input-latex-ts.cpp`, `lambda/tex/`)  
> **Goal:** Full LaTeX document → HTML conversion, written entirely in Lambda Script, reusing the existing tree-sitter LaTeX parser for AST generation

---

## 1. Project Goals & Scope

### 1.1 Primary Goal

Build a **pure Lambda Script package** at `lambda/package/latex/` that converts a LaTeX document AST (produced by tree-sitter-latex + tree-sitter-latex-math via the existing C++ parser) into semantic HTML elements. The package takes parsed Lambda elements as input and produces an `<html>` element tree as output, serializable via `format(result, 'html')`.

### 1.2 Why Migrate from C++ to Lambda Script?

The current C++ LaTeX-to-HTML pipeline spans **~20,600 lines** across 16 files (`input-latex-ts.cpp`, `tex_document_model.*`, `tex_doc_model_html.cpp`, `tex_latex_bridge.*`, `tex_html_render.cpp`, etc.). A Lambda Script package offers:

| Benefit | Detail |
|---------|--------|
| **Prototyping velocity** | Iterate on rendering logic without recompiling (~20× faster edit-test cycle) |
| **User extensibility** | Users can import, override, or extend LaTeX commands — impossible with compiled C++ |
| **Proven pattern** | The math package (2,523 lines Lambda) already replaced ~5,000 lines of C++ math-to-HTML rendering |
| **Declarative mapping** | LaTeX command → HTML element mappings are naturally expressed as Lambda maps and `match` dispatchers |
| **Consistency** | Follows the same module pattern as `math` and `chart` packages |
| **Maintainability** | Lambda's functional style eliminates the memory management, pointer arithmetic, and StrBuf bookkeeping that dominate the C++ implementation |
| **Language showcase** | A non-trivial real-world package validates Lambda's maturity for production use |

### 1.3 Scope

| In Scope | Out of Scope |
|----------|-------------|
| LaTeX document AST → HTML element tree | Parsing LaTeX source (stays in C++ tree-sitter) |
| Document structure (sections, lists, tables, text) | TeX typesetting pipeline (DVI/PDF/SVG output) |
| Inline & display math (delegate to `math` package) | Interactive editing |
| Text formatting (`\textbf`, `\emph`, font commands) | Full TeX macro expansion (`\def`, `\expandafter`) |
| Environments (itemize, enumerate, quote, verbatim, etc.) | TikZ / PGF graphics |
| Cross-references (`\label`, `\ref`, `\cite`) — basic | Full bibliography processing (BibTeX) |
| CSS stylesheet generation | Custom document classes beyond article/book/report |
| Table of contents, figure/table captions | Index generation |
| Diacritics and special characters | Custom font loading |
| Macro definitions (`\newcommand`) — basic expansion | `\catcode` changes and TeX primitives |

### 1.4 Replaces C++ HTML Pipeline

This package **replaces** the C++ document-to-HTML pipeline:

| C++ Component | Lines | Replaced By |
|---------------|-------|-------------|
| `tex_document_model.cpp` | 7,684 | `latex/render.ls`, `latex/structure.ls` |
| `tex_doc_model_html.cpp` | 1,349 | `latex/html.ls`, `latex/elements/` |
| `tex_doc_model_struct.cpp` | 1,017 | `latex/structure.ls` |
| `tex_doc_model_text.cpp` | 372 | `latex/text.ls` |
| `tex_doc_model_commands.cpp` | 396 | `latex/commands.ls` |
| `tex_latex_bridge.cpp/.hpp` | 2,317 | Not needed (direct AST → HTML) |
| `tex_html_render.cpp/.hpp` | 2,776 | Reused via `math` package |
| `tex_document_model.hpp` | 823 | Implicit in Lambda types |
| `math_symbols.cpp/.hpp` | 448 | `latex/symbols.ls` (extended from `math/symbols.ls`) |
| `input-common.cpp` | 191 | `latex/util.ls` |
| `format-latex.cpp` | 214 | Out of scope (LaTeX output formatter) |
| **Total replaced** | **~17,600** | **Est. ~4,000–5,000 lines Lambda** |

The C++ tree-sitter parser (`input-latex-ts.cpp`, 3,052 lines) is **retained** — it remains the LaTeX → Lambda AST conversion layer. The C++ typesetter pipeline (`tex_*` files for DVI/PDF) also remains for publication-quality rendering.

---

## 2. Prior Art Analysis

### 2.1 Reference Implementations

Three implementations were studied to inform the design:

#### LaTeX.js (`ref/latex-js/`) — Lightweight, Single-Pass

| Aspect | Detail |
|--------|--------|
| **Architecture** | PEG.js parser → JavaScript generator (single-pass, inline DOM creation) |
| **Size** | ~3,750 lines (core) |
| **Math** | Delegates entirely to KaTeX |
| **Packages** | ~13 reimplemented (color, graphicx, hyperref, etc.) |
| **Strength** | Speed, simplicity, runs in browser |
| **Weakness** | No intermediate AST, no `\def`/`\if`, limited extensibility |

**Key takeaway:** The argument-type system (`g`, `o?`, `i`, `kv?`) and CSS-variable approach for page geometry are elegant. The group-stack model for font attribute inheritance maps well to Lambda's functional context passing.

#### LaTeXML (`ref/latexml/`) — Full TeX Emulation

| Aspect | Detail |
|--------|--------|
| **Architecture** | 5-phase pipeline: Mouth → Gullet → Stomach → Document → Post-processing |
| **Size** | ~50,000+ lines Perl |
| **Math** | Full semantic math parser → Content + Presentation MathML |
| **Packages** | 452 `.ltxml` binding files |
| **Strength** | Most complete LaTeX→XML/HTML converter in existence (used by arXiv, NIST DLMF) |
| **Weakness** | Very complex, slow, Perl-only |

**Key takeaway:** The **declarative binding system** (`DefConstructor`, `DefMath`, `DefEnvironment`) is a powerful pattern that Lambda maps can replicate. The post-processing pipeline (chain of independent transformers) maps directly to Lambda's pipe operator.

#### Existing C++ Implementation (`lambda/tex/`)

| Aspect | Detail |
|--------|--------|
| **Architecture** | Tree-sitter parse → Lambda Element AST → TexDocumentModel → HTML string output |
| **Size** | ~20,600 lines C++ |
| **Math** | Full TeX typesetter (hlist/vlist/mathlist → HTML via tex_html_render.cpp) |
| **Strength** | Deep integration with Lambda runtime, publication-quality math typesetting |
| **Weakness** | Hard to extend, requires recompilation, memory management overhead |

**Key takeaway:** The two-grammar architecture (text-mode + math-mode) is sound and should be preserved. The `NODE_CLASSIFICATION` table approach for dispatch is directly expressible as a Lambda `match`.

### 2.2 Design Choice: AST-Based Transformation (Best of Both Worlds)

The proposed design combines strengths from all three references:

| Design Decision | Inspired By | Rationale |
|-----------------|-------------|-----------|
| **Two-phase: parse (C++) + transform (Lambda)** | Existing C++ | Tree-sitter parsing stays fast and reliable; transformation moves to Lambda for extensibility |
| **Tag-dispatch via `match`** | Math package | Proven pattern in Lambda — 65+ node types in the math package use it successfully |
| **Context stack (immutable maps)** | LaTeX.js | Font attributes, counters, labels inherited through scope; matches Lambda's functional style |
| **Declarative command registry** | LaTeXML | Command → handler mappings as Lambda maps; users can override/extend |
| **Post-processing pipeline** | LaTeXML | Pipe operator chains: `raw_html | resolve_refs | add_toc | wrap_document` |
| **Math delegation to `math` package** | LaTeX.js (KaTeX) | Don't reinvent — the math package already handles LaTeX math → HTML |
| **CSS class-based styling** | LaTeX.js | Clean DOM, themeable output |

---

## 3. Architecture Design

### 3.1 Overall Pipeline

```
  LaTeX Source (.tex)
        │
        ▼
  ┌─────────────────────────────┐
  │  C++ Layer (retained)       │
  │  input("doc.tex", 'latex)   │
  │  tree-sitter-latex parser   │
  │  tree-sitter-latex-math     │
  │  → Lambda Element AST       │
  └─────────────┬───────────────┘
                │
                ▼
  ┌─────────────────────────────┐
  │  Lambda Package (new)       │
  │  latex.render(ast, options) │
  │                             │
  │  Phase 1: Normalize AST     │
  │  Phase 2: Expand macros     │
  │  Phase 3: Render to HTML    │
  │  Phase 4: Post-process      │
  │  → HTML Element Tree        │
  └─────────────┬───────────────┘
                │
                ▼
  format(result, 'html')
        │
        ▼
  HTML5 Document String
```

### 3.2 Transformation Phases

#### Phase 1: AST Normalization (`normalize.ls`)

Cleans up the raw tree-sitter AST for easier processing:
- Merge adjacent text nodes
- Flatten trivial wrappers (single-child groups)
- Normalize whitespace (collapse multiple spaces, handle `~` → nbsp)
- Convert ligatures (`---` → `\u2014`, `''` → `\u201D`, etc.)
- Strip comment nodes

```lambda
pub fn normalize(ast) {
    ast | normalize_node
}

fn normalize_node(node) {
    if (node is string) normalize_text(node)
    else if (node is element) {
        let tag = name(node)
        match tag {
            case 'ligature':    resolve_ligature(node)
            case 'nbsp':        "\u00A0"
            case 'space':       " "
            default:            normalize_children(node)
        }
    }
    else node
}
```

#### Phase 2: Macro Expansion (`macros.ls`)

Handles `\newcommand` and `\def` at the Lambda level:

```lambda
// registry of user-defined macros
// key: command name, value: {params: int, default: string|null, body: element}
fn collect_macros(ast) {
    ast?<newcommand> | ~
        | {name: get_macro_name(~), params: get_param_count(~), body: get_macro_body(~)}
}

fn expand_macros(node, macros) {
    if (node is string) node
    else if (node is element) {
        let tag = name(node)
        let cmd_name = if (tag == 'command') node.name else null
        if (cmd_name != null and macros[cmd_name] != null)
            apply_macro(macros[cmd_name], node, macros)
        else
            // recurse into children with macro env
            expand_children(node, macros)
    }
    else node
}
```

#### Phase 3: HTML Rendering (`render.ls`)

The core transformation — dispatches on AST element tags to produce HTML elements:

```lambda
pub fn render_node(node, ctx) {
    if (node is string) render_text(node, ctx)
    else if (node is element) dispatch_element(node, ctx)
    else null
}

fn dispatch_element(node, ctx) {
    let tag = name(node)
    match tag {
        // document structure
        case 'latex_document':  structure.render_document(node, ctx, render_node)
        case 'document':        structure.render_body(node, ctx, render_node)
        case 'section':         structure.render_heading(node, ctx, 2, render_node)
        case 'subsection':      structure.render_heading(node, ctx, 3, render_node)
        case 'subsubsection':   structure.render_heading(node, ctx, 4, render_node)
        case 'paragraph':       text.render_paragraph(node, ctx, render_node)

        // text formatting
        case 'textbf':          text.render_styled(node, ctx, "b", render_node)
        case 'textit':          text.render_styled(node, ctx, "i", render_node)
        case 'emph':            text.render_styled(node, ctx, "em", render_node)
        case 'underline':       text.render_styled(node, ctx, "u", render_node)
        case 'texttt':          text.render_code_inline(node, ctx, render_node)
        case 'verb_command':    text.render_verbatim_inline(node, ctx)

        // math (delegate to math package)
        case 'inline_math':     math_bridge.render_inline(node, ctx)
        case 'display_math':    math_bridge.render_display(node, ctx)
        case 'equation':        math_bridge.render_equation(node, ctx)
        case 'align':           math_bridge.render_align(node, ctx)

        // environments
        case 'itemize':         envs.render_list(node, ctx, "ul", render_node)
        case 'enumerate':       envs.render_list(node, ctx, "ol", render_node)
        case 'description':     envs.render_description(node, ctx, render_node)
        case 'quote':           envs.render_blockquote(node, ctx, render_node)
        case 'quotation':       envs.render_blockquote(node, ctx, render_node)
        case 'center':          envs.render_alignment(node, ctx, "center", render_node)
        case 'verbatim':        envs.render_verbatim(node, ctx)
        case 'tabular':         tables.render_tabular(node, ctx, render_node)
        case 'figure':          envs.render_figure(node, ctx, render_node)
        case 'abstract':        envs.render_abstract(node, ctx, render_node)
        case 'multicols':       envs.render_multicol(node, ctx, render_node)

        // references
        case 'label':           refs.record_label(node, ctx)
        case 'ref':             refs.render_ref(node, ctx)
        case 'footnote':        refs.render_footnote(node, ctx, render_node)

        // spacing & breaks
        case 'linebreak_command': <br>
        case 'hskip':          spacing.render_hskip(node)
        case 'vspace':         spacing.render_vspace(node)
        case 'parbreak':       null  // handled by paragraph grouping

        // fallback
        default:               render_default(node, ctx, render_node)
    }
}
```

#### Phase 4: Post-Processing (`postprocess.ls`)

Chain of independent transformers applied after HTML generation:

```lambda
pub fn postprocess(html, ctx) {
    html
    | resolve_cross_refs(~, ctx.labels)
    | insert_toc(~, ctx.headings)
    | insert_footnotes(~, ctx.footnotes)
    | wrap_standalone(~, ctx.options)
}
```

### 3.3 Module Structure

```
lambda/package/latex/
├── latex.ls              # Main entry point: pub fn render(ast, options)    (146 lines) ✅ Done
├── render2.ls            # Core dispatcher: AST tag → handler (stateless)  (1,162 lines) ✅ Done
├── analyze.ls            # Pass 1: counters, headings, labels, theorems    (355 lines) ✅ Done
├── normalize.ls          # Phase 1: AST cleanup and normalization          (206 lines) ✅ Done
├── macros.ls             # Phase 2: \newcommand expansion                  (262 lines) ✅ Done
├── math_bridge.ls        # Bridge to math package for inline/display math  (47 lines) ✅ Done
├── symbols.ls            # LaTeX commands → Unicode/HTML entities          (169 lines) ✅ Done
├── css.ls                # CSS stylesheet (article theme)                  (137 lines) ✅ Done
├── util.ls               # Shared helpers (string, element utilities)      (213 lines) ✅ Done
├── to_html.ls            # Custom HTML serializer (bypasses format/html)   (155 lines) ✅ Done
├── elements/
│   └── spacing.ls        # \hspace, \vspace, \hskip, line/page breaks     (105 lines) ✅ Done
└── test_latex_pkg.ls     # Integration test script                         (58 lines) ✅ Done
```

**Removed legacy modules** (replaced by render2.ls + analyze.ls two-pass architecture):
- ~~`render.ls`~~ — legacy stateful dispatcher (superseded by render2.ls)
- ~~`context.ls`~~ — legacy context threading (superseded by analyze.ls)
- ~~`structure.ls`~~ — legacy document structure (merged into render2.ls)
- ~~`text.ls`~~ — legacy text formatting (merged into render2.ls)
- ~~`elements/lists.ls`~~ — legacy list rendering (merged into render2.ls)
- ~~`elements/environments.ls`~~ — legacy environments (merged into render2.ls)
- ~~`elements/tables.ls`~~ — legacy tables (merged into render2.ls)
- ~~`elements/figures.ls`~~ — never created, logic is in render2.ls
- ~~`elements/refs.ls`~~ — never created, logic is in render2.ls

**Not yet created:**
- `docclass/article.ls`, `book.ls`, `report.ls` — document class modules
- `postprocess.ls` — footnote section post-processing (footnotes currently rendered inline)

**Current size:** ~3,015 lines across 12 source files.

### 3.4 Data Flow Detail

```lambda
// User API
import latex: .lambda.package.latex.latex

// Parse LaTeX source → AST (C++ tree-sitter, unchanged)
let ast = input("paper.tex", 'latex)

// Convert AST → HTML element tree (Lambda package, new)
let html = latex.render(ast, {
    docclass: 'article,
    standalone: true,        // include <html>, <head>, CSS
    math_display: 'html,    // 'html (via math package), 'mathml, or 'katex
    toc: true,               // generate table of contents
    numbering: true          // section numbering
})

// Serialize to HTML string
format(html, 'html)
```

### 3.5 Core Abstractions

#### Context (`context.ls`)

Immutable rendering context threaded through recursion:

```lambda
pub fn make_context(options) => {
    // document state
    docclass: options.docclass or 'article,
    standalone: options.standalone or false,

    // font state (inherited via scope)
    font_family: 'roman,   // roman | sans | mono
    font_weight: 'normal,  // normal | bold
    font_style: 'upright,  // upright | italic
    font_size: 'normalsize,

    // counters
    counters: {
        chapter: 0, section: 0, subsection: 0,
        figure: 0, table: 0, equation: 0,
        footnote: 0, enumi: 0, enumii: 0
    },

    // accumulated state (for post-processing)
    labels: {},       // label_name → {type, number, text}
    headings: [],     // [{level, number, text, id}]
    footnotes: [],    // [{number, content}]

    // options
    math_display: options.math_display or 'html,
    numbering: options.numbering or true
}

// derive a child context with overrides
pub fn derive(parent, overrides) => {
    // ... merge parent with overrides
}
```

#### Math Bridge (`math_bridge.ls`)

Delegates math rendering to the existing `math` package:

```lambda
import math: .lambda.package.math.math

pub fn render_inline(node, ctx) {
    let math_ast = node.ast  // pre-parsed by tree-sitter-latex-math in C++
    if (math_ast != null)
        math.render_inline(math_ast)
    else
        <span class: "math-inline"; node.source>
}

pub fn render_display(node, ctx) {
    let math_ast = node.ast
    let math_html = if (math_ast != null)
        math.render_display(math_ast)
    else
        <span class: "math-display"; node.source>

    <div class: "math-display-container"; math_html>
}

pub fn render_equation(node, ctx) {
    let math_html = render_display(node, ctx)
    let number = if (ctx.numbering) ctx.counters.equation else null
    <div class: "equation";
        math_html
        if (number != null) <span class: "eq-number"; "(" ++ string(number) ++ ")">
    >
}
```

#### Element Construction Patterns

Lambda's native element syntax makes HTML generation natural:

```lambda
// Section heading
fn render_heading(node, ctx, level, render_fn) {
    let title_content = node.title | render_fn(~, ctx)
    let id = slugify(text_content(node.title))
    let number = format_section_number(ctx, level)

    let heading = match level {
        case 1: <h1 id: id; if (number) <span class: "sec-num"; number> title_content>
        case 2: <h2 id: id; if (number) <span class: "sec-num"; number> title_content>
        case 3: <h3 id: id; if (number) <span class: "sec-num"; number> title_content>
        case 4: <h4 id: id; if (number) <span class: "sec-num"; number> title_content>
        default: <h5 id: id; title_content>
    }

    // render children after the heading
    let body = render_children(node, ctx, render_fn)
    (heading, body)
}

// List environment
fn render_list(node, ctx, list_tag, render_fn) {
    let items = node?<item>
    let rendered_items = for (item in items)
        <li; for (child in item[element]) render_fn(child, ctx)>

    if (list_tag == "ul") <ul; rendered_items>
    else <ol; rendered_items>
}

// Tabular environment
fn render_tabular(node, ctx, render_fn) {
    let rows = split_by_row_sep(node)
    <table class: "tabular";
        for (row in rows)
            <tr; for (cell in split_by_col_sep(row))
                <td; for (child in cell) render_fn(child, ctx)>
            >
    >
}
```

---

## 4. Feasibility Analysis

### 4.1 What Lambda Already Provides

| Capability | Status | Evidence |
|-----------|--------|---------|
| **Tree-sitter LaTeX parser** | ✅ Ready | `input("file.tex", 'latex)` produces full AST with 44 text-mode + 51 math-mode node types |
| **Element construction** | ✅ Ready | Native `<tag attr: val; children>` syntax — direct HTML construction |
| **Pattern matching** | ✅ Ready | `match` on element tag names — math package dispatches 65+ node types |
| **Module system** | ✅ Ready | `import`/`pub` proven by math (18 modules) and chart (12 modules) packages |
| **HTML formatter** | ✅ Ready | `format(element, 'html')` serializes element trees |
| **Element traversal** | ✅ Ready | `name(el)`, `el.attr`, `el[i]`, `len(el)`, `el?<tag>` (recursive query) |
| **Higher-order transforms** | ✅ Ready | Pipe `\|`, `where`, `for` comprehensions — natural for tree walking |
| **String operations** | ✅ Ready | `++`, `replace`, `split`, `trim`, `contains`, `starts_with`, `upper`/`lower`, `slice`, `chars` |
| **Map construction** | ✅ Ready | Static `{k: v}` and dynamic `map([k,v,...])` — ideal for lookup tables and context objects |
| **Float arithmetic** | ✅ Ready | Needed for spacing calculations |
| **Recursive functions** | ✅ Ready | Essential for tree walking; tail-call optimized |
| **Math package** | ✅ Ready | 2,523 lines already handle LaTeX math → HTML; this package simply delegates |

### 4.2 Feature Gap Analysis

| Gap                                  | Severity | Impact on LaTeX Package                                                               | Workaround                                                                                                                                                                       |
| ------------------------------------ | -------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **String pattern matching**          | ✅ Available | Lambda has string patterns (equivalent to regex) for matching command names, extracting substrings, etc. | No workaround needed — string patterns cover all pattern-matching use cases for LaTeX command dispatch. |
| **No mutable maps in `fn`**          | Low      | Counter state (section numbers, equation numbers) must be threaded through recursion. | Return updated context from each render function (functional state threading). The math package already does this with immutable context passing. The entire transformation should remain purely functional. |
| **No string interpolation**          | Low      | HTML attribute generation requires verbose `++` concatenation.                        | `"section-" ++ string(level) ++ "-" ++ id` works, just more verbose. Acceptable for a package.                                                                                   |
| **No `reduce`/`fold` built-in**      | Low      | Accumulating state across a list of nodes (counters, labels).                         | Implement as a recursive `fn`. Keeps the entire transformation purely functional.                                                                                                |
| **No `try`/`catch`**                 | Low      | Graceful error recovery on malformed LaTeX.                                           | Error-as-value model (`T^E` return types). Tree-sitter already handles parse error recovery at the grammar level.                                                                |
| **Limited element mutation in `fn`** | Low      | Can't modify element attributes in pure functions.                                    | Construct new elements with desired attributes. Functional construction is idiomatic in Lambda anyway.                                                                           |

### 4.3 Good-to-Have Language Enhancements

These features would significantly improve the LaTeX package implementation quality, though none are blockers:

| Feature                               | Impact | Use Case                                                                                                                                               |
| ------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **`str_repeat(s, n)` built-in**       | Low    | Constructing indentation, repeated separators. Already available as recursive helper.                                                                  |
| **Mutable counter in `fn` scope**     | Medium | Section/equation numbering across siblings. Currently requires threading updated counter maps through every call.                                      |
| **`find(array, predicate)` built-in** | Low    | Finding specific child elements. Achievable with `where` + index-0.                                                                                    |
| **Dynamic element tag construction**  | Medium | Creating `<h1>` through `<h6>` from a variable. Currently requires match arms for each level. Consider: `element(tag_name, attrs, children)` function. |
| **Map merge operator**                | ✅ Available | `{parent_ctx, counter: new_val}` — merges parent context with overrides. Spread `{*parent_ctx, counter: new_val}` also works but is not needed.       |
| **Multi-line raw strings**            | Low    | Embedding CSS stylesheets. Already supported via `++` concatenation of string literals.                                                                |

### 4.4 Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| **Performance on large documents** | Medium | Lambda JIT (MIR) compiles to native code. The math package handles complex expressions without issues. A 50-page paper should process in < 1 second. Profile early with a benchmark document. |
| **Counter/state threading complexity** | Medium | The biggest architectural challenge. LaTeX's stateful counter model (`\refstepcounter`, `\setcounter`) clashes with pure functional style. Use a two-pass approach: first pass collects all counters/labels, second pass renders with complete state. |
| **AST coverage gaps** | Low | The tree-sitter LaTeX grammar covers all common constructs. The C++ `input-latex-ts.cpp` already maps 44+ node types for text mode and 51+ for math mode. Any AST gap is a tree-sitter grammar issue, not a Lambda package issue. |
| **Edge cases in macro expansion** | Medium | Only basic `\newcommand` expansion is in scope. Complex TeX macros (`\def` with `\expandafter`, `\csname`) are not supported — this is consistent with LaTeX.js's pragmatic approach and covers 95% of real documents. |

---

## 5. Detailed Design

### 5.1 AST Input Format

The C++ parser (`input-latex-ts.cpp`) produces a tree of Lambda elements. The latex package consumes this tree directly. Key node types and their attributes:

**Document structure:**

```
<latex_document;
    <documentclass; "article">         // preamble
    <usepackage name: "amsmath">
    <newcommand; "\mycmd" <curly_group; "definition">>
    <document;                          // \begin{document}...\end{document}
        <section title: <curly_group; "Introduction">;
            <paragraph; "Text here" <textbf; "bold"> "more text.">
            <paragraph; "Second paragraph with " <inline_math source: "x^2", ast: <math ...>>>
        >
        <section title: <curly_group; "Methods">;
            <itemize;
                <item; "First item">
                <item; "Second item">
            >
        >
    >
>
```

**Math nodes** (pre-parsed by tree-sitter-latex-math):

```
<inline_math source: "x^2 + y^2 = z^2", ast: <math;
    <subsup base: <symbol; "x">, sup: <number; "2">>
    <operator; "+">
    <subsup base: <symbol; "y">, sup: <number; "2">>
    <relation; "=">
    <subsup base: <symbol; "z">, sup: <number; "2">>
>>
```

### 5.2 HTML Output Format

The package produces semantic HTML5 with CSS classes:

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Paper Title</title>
    <style>/* LaTeX-like CSS */</style>
</head>
<body>
<article class="latex-document latex-article">
    <header class="latex-title">
        <h1 class="title">Paper Title</h1>
        <div class="author">Author Name</div>
        <div class="date">2026</div>
    </header>

    <section id="sec-introduction">
        <h2><span class="sec-num">1</span> Introduction</h2>
        <p>Text here <strong>bold</strong> more text.</p>
        <p>Second paragraph with <span class="ML__latex">...</span></p>
    </section>

    <section id="sec-methods">
        <h2><span class="sec-num">2</span> Methods</h2>
        <ul>
            <li>First item</li>
            <li>Second item</li>
        </ul>
    </section>
</article>
</body>
</html>
```

### 5.3 CSS Design

The package generates a self-contained CSS stylesheet that approximates LaTeX's typographic conventions:

```lambda
// css.ls
pub let STYLESHEET = "
.latex-document {
    max-width: 800px;
    margin: 0 auto;
    padding: 2em;
    font-family: 'Computer Modern Serif', 'Latin Modern Roman', Georgia, serif;
    font-size: 12pt;
    line-height: 1.5;
}
.latex-title .title { font-size: 1.7em; font-weight: bold; text-align: center; }
.latex-title .author { text-align: center; font-size: 1.1em; margin: 0.5em 0; }
.sec-num { margin-right: 0.5em; }
.latex-abstract { margin: 1em 3em; font-size: 0.95em; }
.latex-abstract .abstract-title { font-weight: bold; text-align: center; }
.latex-blockquote { margin: 1em 2em; }
.latex-verbatim { font-family: monospace; white-space: pre; background: #f5f5f5; padding: 0.5em; }
.latex-footnote-ref { vertical-align: super; font-size: 0.8em; }
table.tabular { border-collapse: collapse; margin: 1em auto; }
table.tabular td, table.tabular th { padding: 0.3em 0.8em; }
table.tabular .hline { border-bottom: 1px solid black; }
.math-display-container { text-align: center; margin: 1em 0; }
.equation { display: flex; align-items: center; justify-content: center; }
.eq-number { margin-left: auto; }
.multicol { column-count: 2; column-gap: 2em; }
"

pub fn wrap_with_css(body, options) {
    <html lang: "en";
        <head;
            <meta charset: "UTF-8">
            <title; options.title or "LaTeX Document">
            <style; STYLESHEET>
            <style; math.stylesheet()>  // math package CSS
        >
        <body; body>
    >
}
```

### 5.4 Document Class System

Following LaTeX.js's approach, document class-specific behavior is encapsulated in separate modules:

```lambda
// docclass/article.ls
pub let SECTION_HIERARCHY = ['section, 'subsection, 'subsubsection, 'paragraph, 'subparagraph]
pub let TOP_LEVEL_HEADING = 2  // <h2> for \section

pub fn format_section_number(counters, level) {
    match level {
        case 0: string(counters.section)
        case 1: string(counters.section) ++ "." ++ string(counters.subsection)
        case 2: string(counters.section) ++ "." ++ string(counters.subsection) ++ "." ++ string(counters.subsubsection)
        default: null
    }
}

// docclass/book.ls
pub let SECTION_HIERARCHY = ['chapter, 'section, 'subsection, 'subsubsection, 'paragraph]
pub let TOP_LEVEL_HEADING = 1  // <h1> for \chapter

pub fn format_section_number(counters, level) {
    match level {
        case 0: string(counters.chapter)
        case 1: string(counters.chapter) ++ "." ++ string(counters.section)
        // ...
        default: null
    }
}
```

### 5.5 Cross-Reference Resolution

Cross-references require a two-pass approach:

```lambda
// Pass 1: Collect all labels and their numbering context
fn collect_labels(ast, ctx) {
    // walk tree, accumulate {label_name: {type, number, id}} into ctx.labels
}

// Pass 2: Resolve \ref commands using collected labels
fn resolve_refs(html, labels) {
    // walk HTML tree, replace <ref target: "name"> with <a href: "#id">number</a>
}
```

### 5.6 Command Registry Pattern

Extensible command handling via map-based dispatch:

```lambda
// commands.ls — built-in command handlers
let SIMPLE_COMMANDS = {
    "textbf":       fn(node, ctx, rf) => <strong; render_children(node, ctx, rf)>,
    "textit":       fn(node, ctx, rf) => <em; render_children(node, ctx, rf)>,
    "emph":         fn(node, ctx, rf) => <em; render_children(node, ctx, rf)>,
    "underline":    fn(node, ctx, rf) => <u; render_children(node, ctx, rf)>,
    "texttt":       fn(node, ctx, rf) => <code; render_children(node, ctx, rf)>,
    "textsf":       fn(node, ctx, rf) => <span class: "sf"; render_children(node, ctx, rf)>,
    "textsc":       fn(node, ctx, rf) => <span class: "sc"; render_children(node, ctx, rf)>,
    "textrm":       fn(node, ctx, rf) => <span class: "rm"; render_children(node, ctx, rf)>,
    "textsl":       fn(node, ctx, rf) => <span class: "sl"; render_children(node, ctx, rf)>,
    "centering":    fn(node, ctx, rf) => <div class: "center"; render_children(node, ctx, rf)>,
    "raggedright":  fn(node, ctx, rf) => <div class: "flushleft"; render_children(node, ctx, rf)>,
    "raggedleft":   fn(node, ctx, rf) => <div class: "flushright"; render_children(node, ctx, rf)>,
    "tiny":         fn(node, ctx, rf) => <span class: "tiny"; render_children(node, ctx, rf)>,
    "small":        fn(node, ctx, rf) => <span class: "small"; render_children(node, ctx, rf)>,
    "large":        fn(node, ctx, rf) => <span class: "large"; render_children(node, ctx, rf)>,
    "Large":        fn(node, ctx, rf) => <span class: "Large"; render_children(node, ctx, rf)>,
    "huge":         fn(node, ctx, rf) => <span class: "huge"; render_children(node, ctx, rf)>,
    "Huge":         fn(node, ctx, rf) => <span class: "Huge"; render_children(node, ctx, rf)>,
}

// Users can extend:
// let custom_commands = {(SIMPLE_COMMANDS), "mycommand": fn(n,c,r) => <div; ...>}
```

---

## 6. Implementation Roadmap

### Phase 1: Core Document Structure (MVP) — ✅ DONE

**Goal:** Render a basic LaTeX article to readable HTML.

| Module | Features | Lines | Status |
|--------|----------|-------|--------|
| `latex.ls` | Entry point, API | 146 | ✅ Done |
| `render2.ls` | Core dispatcher (90+ tags, stateless two-pass) | 1,162 | ✅ Done |
| `analyze.ls` | Pass 1: counters, headings, labels, figures, tables, theorems, bibitems | 355 | ✅ Done |
| `normalize.ls` | AST cleanup, ligatures, whitespace | 206 | ✅ Done |
| `math_bridge.ls` | Delegate to math package | 47 | ✅ Done |
| `symbols.ls` | Diacritics, special characters | 169 | ✅ Done |
| `css.ls` | Article stylesheet | 137 | ✅ Done |
| `util.ls` | Helpers (slugify, text_content, etc.) | 213 | ✅ Done |
| `to_html.ls` | Custom HTML serializer | 155 | ✅ Done |
| **Phase 1 Total** | | **~2,590** | ✅ |

**Milestone test:** ✅ Renders `\documentclass{article}\begin{document}\section{Hello}\textbf{World}\end{document}` to valid HTML.

### Phase 2: Environments & Lists — ✅ DONE (merged into render2.ls)

| Module | Features | Status |
|--------|----------|--------|
| Lists (in render2.ls) | itemize, enumerate, description | ✅ Done |
| Environments (in render2.ls) | quote, center, verbatim, abstract, multicols, minipage | ✅ Done |
| Tables (in render2.ls) | tabular (basic row/cell splitting) | ✅ Done |
| Figures (in render2.ls) | figure, caption, includegraphics, auto-numbering | ✅ Done |
| Table floats (in render2.ls) | table environment, caption, auto-numbering | ✅ Done |
| Theorem environments (in render2.ls) | theorem, lemma, corollary, proposition, definition, example, remark, proof (8 types with auto-numbering) | ✅ Done |
| Spacing (`elements/spacing.ls`) | hspace, vspace, breaks | ✅ Done |

**Note:** These were implemented directly in render2.ls rather than separate element modules, since the two-pass architecture eliminated the need for context-threading callbacks.

### Phase 3: Cross-References & Post-Processing — ✅ DONE

| Module | Features | Status |
|--------|----------|--------|
| Cross-refs (in render2.ls + analyze.ls) | `\label`, `\ref`, `\href`, `\url`, `\footnote` | ✅ Done |
| `macros.ls` | `\newcommand` basic expansion (single/multi-param, optional args, nested) | ✅ Done (262 lines) |
| Footnotes (in latex.ls) | Footnote section appended at end of document | ✅ Done |

### Phase 4: Document Classes & Polish — ❌ TODO

| Module | Features | Status |
|--------|----------|--------|
| `docclass/article.ls` | Article numbering, layout | ❌ TODO |
| `docclass/book.ls` | Chapters, parts, front/back matter | ❌ TODO |
| `docclass/report.ls` | Report-specific features | ❌ TODO |
| Additional CSS themes | | ❌ TODO |

### Phase 5: Advanced Features — 🔶 PARTIAL

| Feature | Status |
|---------|--------|
| Tabular with column alignment (`l`/`c`/`r`), `\hline` handling | ✅ Done |
| Bibliography (`\cite` → `[N]` links, `\thebibliography`, `\bibitem`) | ✅ Done |
| Hyperlinks (`\href`, `\url`) | ✅ Done |
| Figure/table auto-numbering (analyze pass) | ✅ Done |
| Theorem environments with auto-numbering (8 types) | ✅ Done |
| Tabular multicolumn/multirow | ❌ TODO |
| Custom macro expansion (multi-parameter) | ✅ Done (in macros.ls) |
| Error recovery and warnings | ❌ TODO |

### Grand Total

| Phase | Lines | Status |
|-------|-------|--------|
| Phase 1: Core (latex, render2, analyze, normalize, math_bridge, symbols, css, util, to_html) | 2,590 | ✅ Done |
| Phase 2: Environments (merged into render2.ls + elements/spacing.ls) | 105 | ✅ Done |
| Phase 3: Cross-Refs & Macros (macros.ls) | 262 | ✅ Done |
| Phase 4: Doc Classes | TBD | ❌ TODO |
| Phase 5: Advanced (tabular, bibliography, theorems — in render2.ls + analyze.ls) | — | 🔶 Partial |
| Integration test (test_latex_pkg.ls) | 58 | ✅ Done |
| **Actual Total** | **3,015** | **12 files** |

**Actual: 3,015 lines** of Lambda Script across 12 source files. Well within the original estimate of ~4,000–5,000 lines, with most features already implemented.

---

## 7. Testing Strategy

### 7.1 Unit Tests

Each module gets a test script under `test/lambda/`:

```
test/lambda/
├── test_latex_pkg_structure.ls      # Section numbering, headings, title
├── test_latex_pkg_text.ls           # Text formatting commands
├── test_latex_pkg_lists.ls          # List environments
├── test_latex_pkg_tables.ls         # Tabular rendering
├── test_latex_pkg_math.ls           # Math bridge (inline, display, equation)
├── test_latex_pkg_refs.ls           # Cross-references, footnotes
├── test_latex_pkg_macros.ls         # Macro expansion
├── test_latex_pkg_normalize.ls      # AST normalization
└── test_latex_pkg_e2e.ls            # End-to-end: .tex source → HTML output
```

### 7.2 Snapshot Tests

Compare HTML output against reference files:

```lambda
// test_latex_pkg_e2e.ls
let ast = input("test/latex/fixtures/article_basic.tex", 'latex)
let html = latex.render(ast, {standalone: true})
let result = format(html, 'html)
let expected = input("test/latex/fixtures/article_basic.html")
assert(result == expected, "article_basic HTML mismatch")
```

### 7.3 Regression Against C++ Output

During migration, validate Lambda output against existing C++ output:

```bash
# C++ path
./lambda.exe convert test.tex -t html -o output_cpp.html

# Lambda path
./lambda.exe -e 'import latex: .lambda.package.latex.latex
let ast = input("test.tex", 'latex)
format(latex.render(ast, {standalone: true}), 'html)' > output_lambda.html

# Compare
diff output_cpp.html output_lambda.html
```

---

## 8. Migration Plan

### 8.1 Coexistence Strategy

The Lambda package and C++ pipeline will coexist during migration:

1. **Phase 1–3:** Lambda package developed independently. C++ pipeline remains the default for `convert ... -t html`.
2. **Phase 4:** Add a CLI flag or option to select the Lambda pipeline: `--engine=lambda` vs `--engine=cpp`.
3. **Phase 5:** Lambda pipeline becomes default. C++ HTML pipeline deprecated (C++ DVI/PDF pipeline retained).

### 8.2 C++ Runtime Integration

The Lambda package needs the following from the C++ runtime:

| Requirement | Status | Notes |
|-------------|--------|-------|
| `input(file, 'latex)` → AST | ✅ Exists | Uses tree-sitter-latex + tree-sitter-latex-math |
| `format(el, 'html)` → string | ✅ Exists | Standard HTML formatter |
| `math.render_math(ast)` → HTML | ✅ Exists | Math package already operational |
| Module import system | ✅ Exists | Proven by math and chart packages |

No new C++ runtime features are required.

---

## 9. Comparison Summary

| Aspect | C++ (current) | Lambda (proposed) | LaTeX.js | LaTeXML |
|--------|---------------|-------------------|----------|---------|
| **Language** | C++ | Lambda Script | JavaScript | Perl |
| **Lines of code** | ~20,600 | 3,015 (actual) | ~3,750 | ~50,000+ |
| **Parser** | Tree-sitter (compiled) | Same (reused) | PEG.js | Full TeX emulator |
| **Math rendering** | TeX typesetter → HTML | Math package (Lambda) | KaTeX | Own MathML |
| **Extensibility** | Recompile required | Import & override | JS classes | `.ltxml` bindings |
| **Package support** | N/A | Extensible via Lambda | ~13 packages | 452 packages |
| **Edit-test cycle** | ~30s (compile + run) | ~1s (interpret) | ~1s | ~5s |
| **Output quality** | Publication-grade | Web-grade (semantic HTML) | Web-grade | Publication-grade |
| **Macro expansion** | None | Basic `\newcommand` | None | Full TeX |

---

## 10. Conclusion

The migration from C++ to Lambda Script for LaTeX-to-HTML conversion is **feasible and well-motivated**:

1. **Proven pattern** — The math package successfully replaced 5,000 lines of C++ with 2,523 lines of Lambda Script for math rendering.
2. **No parser changes** — The existing tree-sitter LaTeX grammars and C++ parser are retained unchanged.
3. **Language readiness** — Lambda has all essential features: element construction, pattern matching, module system, pipes, recursion, and string processing.
4. **6:1 code reduction** — 3,015 lines of Lambda (and growing) replacing ~17,600 lines of C++ for the HTML path, exceeding the original 4:1 estimate.
5. **User extensibility** — Users can import the package and override command handlers, add custom environments, or modify the stylesheet.
6. **Low risk** — C++ pipeline is retained for DVI/PDF output. Migration is incremental and reversible.

**Current status (as of implementation):** Phases 1–3 complete, Phase 5 mostly complete. The package handles document structure, text formatting, math, lists, environments, figures, tables, theorems, bibliography, cross-references, macro expansion, and tabular rendering with column alignment. 428/428 baseline tests pass. Remaining work: document class modules (Phase 4), multicolumn/multirow tabular, and error recovery.

---

## 11. Issues Encountered While Working with Lambda

This section documents Lambda language issues discovered during the LaTeX package implementation. These are real bugs or surprising behaviors that required workarounds. Each is categorized as **Fixed** (resolved in Lambda), **Worked Around** (still present, code uses alternative approach), or **Outstanding** (still affecting development).

### 11.1 Operator Precedence: `is` binds tighter than `and`

**Status:** Fixed (grammar.js — moved `and`/`or` below `is`/`in` in precedence table)  
**Severity:** High — caused silent logic errors

`is` and `in` previously had lower precedence than `and`/`or`, so `x is element and len(x) > 0` was parsed as `x is (element and len(x) > 0)`. Fixed by reordering the precedence list in `grammar.js` so that `is`/`in` bind tighter than `and`/`or`.

```lambda
// NOW CORRECT — parsed as: (node is element) and (name(node) == 'section')
if (node is element and name(node) == 'section') ...

// Explicit parentheses still work and improve readability
if ((node is element) and name(node) == 'section') ...
```

**Resolution:** The existing parenthesized code in macros.ls and render2.ls remains correct and is kept for readability.

**Precedent in other languages:** The fix aligns with the universal convention — every mainstream language puts type-check/membership operators above logical operators:

| Language | `is`/`in` precedence | `and`/`or` precedence | Order |
|----------|---------------------|----------------------|-------|
| **Python** | `is`, `in`, `not in` at comparison level | `and` below, `or` below `and` | `is` > `and` > `or` |
| **Kotlin** | `is`, `in` at relational level (`<`, `>`) | `&&` below comparisons, `\|\|` below `&&` | `is` > `&&` > `\|\|` |
| **C#** | `is` at relational level | `&&` two levels below, `\|\|` below that | `is` > `&&` > `\|\|` |
| **TypeScript** | `instanceof`, `in` at relational level | `&&` below equality, `\|\|` below `&&` | `in` > `&&` > `\|\|` |
| **Dart** | `is` at relational level | `&&` below equality, `\|\|` below `&&` | `is` > `&&` > `\|\|` |
| **Swift** | `is` at cast level (above comparison) | `&&` below ternary, `\|\|` below `&&` | `is` > `&&` > `\|\|` |

The rationale is that `x is T and y > 0` should naturally read as two independent conditions joined by `and` — type-check and membership operators group with comparisons/relationals, which always bind tighter than logical connectives.

### 11.2 Inline `++` in Map Spread Produces Null

**Status:** Worked Around  
**Severity:** High — produces null map entries silently

Using the array concatenation operator `++` inline within a map literal that also uses spread causes the entry to become null:

```lambda
// BROKEN — the 'items' key becomes null
let new_info = {info, items: info.items ++ [new_item]}

// WORKAROUND — extract to a let binding first
let updated_items = info.items ++ [new_item]
let new_info = {info, items: updated_items}
```

**Impact:** Affected analyze.ls extensively — every map update that accumulated items into arrays had to be split into two statements.

### 11.3 `type` as Map Key Conflicts with Built-in

**Status:** Worked Around  
**Severity:** Medium — parse error

Lambda reserves `type` as a keyword. Using it as a map key causes a parse error:

```lambda
// BROKEN — parse error
let info = {type: "section", number: 1}

// WORKAROUND — use a different key name
let info = {kind: "section", number: 1}
```

**Impact:** All `DocInfo` and label maps had to use `kind` instead of the more natural `type` key.

### 11.4 `name()` Returns a Symbol, Not a String

**Status:** Worked Around  
**Severity:** High — causes silent comparison failures

The `name(element)` function returns a **symbol** (single-quoted), not a string (double-quoted). Comparing with a double-quoted string silently fails — no error, just always `false`:

```lambda
// BROKEN — name() returns 'paragraph (symbol), "paragraph" is a string, never equal
if (name(child) == "paragraph") ...   // always false, no error

// WORKAROUND — compare with a symbol literal (single-quoted)
if (name(child) == 'paragraph) ...    // correct
```

**Impact:** This was the root cause of the tabular rendering producing empty output. The `find_tabular_content` function couldn't find the `paragraph` child because every `name()` comparison silently failed. Debugging required dumping the AST to JSON to discover the type mismatch.

### 11.5 ~~Indexed `for` Comprehension Broken for Arrays~~ (NOT A BUG)

**Status:** Resolved — was a misdiagnosis  
**Severity:** N/A

Originally believed that `for (idx, item in arr)` produced garbage values. Re-testing confirms indexed `for` works correctly on both arrays and element children with mixed types:

```lambda
// ALL CORRECT:
for (i, v in [10, 20, 30]) string(i) ++ ":" ++ string(v)
// ["0:10", "1:20", "2:30"]

for (i, v in ["a", "b", "c"]) if (i == 2) "Z" else v
// ["a", "b", "Z"]
```

The original failures were caused by **11.4** (`name()` returning symbols, not strings), which made child-matching logic silently fail and was misattributed to indexed `for`. The recursive `build_tds` workaround in render2.ls has been simplified back to an indexed `for` comprehension.

**Note:** Indexed `for` over elements with only string children does produce unexpected results because Lambda merges adjacent string children into a single text node during element construction. This is element construction behavior, not a `for` bug.

### 11.6 `trim()` Empty String Comparison Broken

**Status:** Worked Around  
**Severity:** Medium — incorrect boolean result

After `trim()`, comparing the result to an empty string `""` returns `false` even when the string has length 0:

```lambda
let s = trim("\n")
len(s) == 0     // true
s == ""          // false (!)

// WORKAROUND — always use len() for empty checks after trim
if (len(trim(text)) == 0) ...   // correct
```

**Impact:** Caused trailing empty rows in tabular output. The whitespace-only strings between `\hline` and `\end{tabular}` were trimmed to zero-length but not recognized as empty, creating phantom table rows.

### 11.7 ~~`if-else` Inside `for` Comprehension Produces Nulls~~ (NOT A BUG)

**Status:** Resolved — was a misdiagnosis  
**Severity:** N/A

Originally believed that `if-else` inside `for` comprehensions produced all-null results. Re-testing confirms this works correctly:

```lambda
// ALL CORRECT:
for (item in [1, 2, 3]) if (item == 2) "found" else "not"
// ["not", "found", "not"]

for (i, v in [10, 20, 30]) if (i == 1) "mid" else string(v)
// ["10", "mid", "30"]
```

The original failures were likely caused by other issues in the surrounding code (e.g., **11.4** symbol vs string comparisons) rather than the `for` comprehension itself. Helper function extraction is still a valid style choice for readability but is not required as a workaround.

### 11.8 Map Literal After `if` Parsed as Block

**Status:** Worked Around  
**Severity:** Medium — parse ambiguity

A map literal `{key: val}` immediately after `if (condition)` is parsed as a block statement rather than a map expression:

```lambda
// BROKEN — {counters: new_val, ...} parsed as a block
if (has_caption)
    {info, figure_count: info.figure_count + 1}
else
    info

// WORKAROUND — extract to a helper function
fn update_figure_count(info) {
    let new_count = info.figure_count + 1
    {info, figure_count: new_count}
}
```

**Impact:** Several functions in analyze.ls had to be restructured to avoid returning map literals directly from `if` branches.

### 11.9 Summary Table

| #    | Issue                      | Severity | Status        | Workaround Cost                     |
| ---- | -------------------------- | -------- | ------------- | ----------------------------------- |
| 11.1 | `is`/`and` precedence      | High     | Fixed         | Grammar fix — reordered precedence  |
| 11.2 | `++` in map spread → null  | High     | Worked Around | Medium — split into let bindings    |
| 11.3 | `type` as map key          | Medium   | Worked Around | Low — rename to `kind`              |
| 11.4 | `name()` returns symbol    | High     | Worked Around | Low — use single-quoted comparisons |
| 11.5 | ~~Indexed `for` broken~~   | N/A      | Not a Bug     | N/A — works correctly               |
| 11.6 | `trim() == ""` false       | Medium   | Worked Around | Low — use `len()` check             |
| 11.7 | ~~`if-else` in `for`~~     | N/A      | Not a Bug     | N/A — works correctly               |
| 11.8 | Map literal after `if`     | Medium   | Worked Around | Medium — restructure code           |

**Overall assessment:** Despite these issues, Lambda proved viable for a non-trivial package (3,015 lines). The workarounds are manageable — mostly requiring explicit parentheses, let bindings, or helper functions. Issues **11.5** and **11.7** were originally reported as critical bugs but re-testing confirmed they work correctly — the original failures were misattributed symptoms of **11.4** (`name()` returning symbols). The remaining real issues (**11.1**, **11.2**, **11.4**, **11.6**, **11.8**) all have low-cost workarounds.
