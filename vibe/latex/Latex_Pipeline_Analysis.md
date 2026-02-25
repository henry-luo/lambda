# LaTeX Pipeline Analysis: Lambda vs LaTeXML

## Executive Summary

The comparison tests reveal that Lambda's HTML output has **0% token-based similarity** to LaTeXML's output, even after class name normalization. This is due to fundamental structural differences, not just naming conventions. The test infrastructure is now correctly using the unified pipeline and producing semantic HTML.

## Test Results Summary

After fixing the test to use the unified pipeline:
- **7 tests PASSED** (utility tests + individual tests)
- **71 tests FAILED** (all parameterized fixture comparisons)
- **Similarity scores**: 0% across all fixtures

## Current Architecture

### Lambda's Two Pipelines

1. **Raw Element Pipeline** (`format_html` on LaTeX AST)
   - Used by: `test_latexml_compare_gtest.cpp` via `format_html()`
   - Output: Raw element dumps like `<latex_document><section>...</section></latex_document>`
   - Status: ❌ Not suitable for production HTML

2. **Unified Document Model Pipeline** (`doc_model_to_html`)
   - Used by: `./lambda.exe convert ... -t html` 
   - Output: Semantic HTML like `<article class="latex-document"><h3>...</h3><p>...</p></article>`
   - Status: ✅ Closer to production quality but incomplete

### LaTeXML's Architecture

LaTeXML produces:
- Semantic HTML5 with ARIA-like semantic classes (`ltx_section`, `ltx_paragraph`)
- Proper sectioning with `<section>`, `<article>`, `<h1>`-`<h6>` hierarchy
- MathML for math expressions (`<math display="inline">`)
- Cross-reference resolution (`<a href="#S1">`)
- Figure/table handling with captions

## Key Gaps Analysis

### 1. **Test Infrastructure Issue** (Critical)

The test uses `format_html()` which dumps raw LaTeX AST elements, not the unified pipeline:

```cpp
// Current (wrong):
String* html_str = format_html(input->pool, input->root);
// Produces: <latex_document><section>...</section></latex_document>

// Should use (correct):
tex::TexDocumentModel* doc = tex::doc_model_from_string(...);
tex::doc_model_to_html(doc, html_buf, opts);
// Produces: <article class="latex-document"><h3>...</h3></article>
```

**Fix**: Update the test to use the unified pipeline.

### 2. **Cross-Reference Resolution** (High Priority)

| Feature | LaTeXML | Lambda (Current) |
|---------|---------|------------------|
| `\ref{sec:one}` | `<a href="#S1">1</a>` | `sec:one` (raw label text) |
| `\label{fig:1}` | Creates anchor | Label text leaked to output |
| Figure numbering | Automatic | Missing |
| Section numbering | `<span class="ltx_tag">1</span>` | Partial support |

**Current Code Issue** (from `/tmp/out.html`):
```html
<p>sec:one Section One. As we will see in ﬁg_three.</p>
```

Labels are being rendered as text instead of being processed as metadata.

### 3. **Semantic Structure Mapping** (High Priority)

| LaTeX | LaTeXML HTML | Lambda HTML (Current) |
|-------|--------------|----------------------|
| `\section{One}` | `<section><h2>1 One</h2>...</section>` | `<h3>One</h3>` (no wrapping section) |
| `\paragraph` | `<div class="ltx_para"><p>...</p></div>` | `<p class="latex-paragraph">` |
| `\begin{figure}` | `<figure class="ltx_figure">...</figure>` | `<h6>Hi!</h6>` (broken) |
| `\caption{...}` | `<figcaption>Figure 1: ...</figcaption>` | Missing |

### 4. **Math Rendering** (Medium Priority)

| Feature | LaTeXML | Lambda |
|---------|---------|--------|
| Inline math | MathML `<math display="inline">` | SVG or preserved LaTeX |
| Display math | MathML `<math display="block">` | SVG or preserved LaTeX |
| Equation numbers | Automatic | Missing |
| AMS environments | Full support | Partial |

Lambda uses SVG rendering via the TexNode system, which produces visually correct output but differs structurally from MathML.

### 5. **Font Styling** (Low Priority)

| Feature | LaTeXML | Lambda |
|---------|---------|--------|
| `\emph` | `<em class="ltx_emph ltx_font_italic">` | `<i>` or style span |
| `\textbf` | `<span class="ltx_text ltx_font_bold">` | `<b>` or style span |
| Nested emphasis | Toggles upright/italic | Basic nesting |

## Proposed Structural Improvements

### Phase 1: Fix Test Infrastructure (Immediate)

Update `test_latexml_compare_gtest.cpp` to use the unified pipeline:

```cpp
std::string latex_to_html(const std::string& latex_content, const std::string& filename) {
    // Create arena for document model  
    Pool* doc_pool = pool_create();
    Arena* doc_arena = arena_create_default(doc_pool);
    
    // Build document model (unified pipeline)
    tex::TexDocumentModel* doc = tex::doc_model_from_string(
        latex_content.c_str(), latex_content.size(), doc_arena, nullptr);
    
    if (!doc || !doc->root) {
        arena_destroy(doc_arena);
        pool_destroy(doc_pool);
        return "";
    }
    
    // Render to HTML
    StrBuf* html_buf = strbuf_new_cap(8192);
    tex::HtmlOutputOptions opts = tex::HtmlOutputOptions::defaults();
    opts.standalone = false;  // No DOCTYPE/head/body wrapper
    opts.pretty_print = false;
    
    tex::doc_model_to_html(doc, html_buf, opts);
    
    std::string result(html_buf->str, html_buf->length);
    
    strbuf_free(html_buf);
    arena_destroy(doc_arena);
    pool_destroy(doc_pool);
    
    return result;
}
```

### Phase 2: Cross-Reference System (1-2 weeks)

1. **Label Collection Pass**: Before HTML generation, scan document for all `\label{}` commands and build a label→reference map.

2. **Reference Resolution**: When encountering `\ref{label}`, look up in the map and generate proper anchor links.

3. **Implementation Location**: Enhance `tex_document_model.cpp`:
   - `add_label()` - already exists
   - `resolve_ref()` - already exists but not used during AST→DocModel conversion

### Phase 3: Sectioning Hierarchy (1 week)

Current issue: `\section`, `\subsection` generate headings but don't create semantic wrappers.

Fix in `tex_document_model.cpp`:

```cpp
// When building section, wrap content until next same-or-higher-level heading
case DocElemType::HEADING:
    // Create <section> wrapper
    DocElement* section_wrapper = alloc_doc_element(arena, DocElemType::SECTION);
    section_wrapper->add_child(heading);
    // Collect following content until next heading at same or higher level
    ...
```

### Phase 4: Figure/Table Handling (1 week)

Current issue: `\begin{figure}...\caption{...}\end{figure}` produces broken output.

Fix: Proper `DocElemType::FIGURE` handling with caption extraction:

```cpp
case DocElemType::FIGURE:
    strbuf_append_str(out, "<figure class=\"latex-figure\">\n");
    // Render content (excluding caption)
    for (child : elem->children) {
        if (child->type != DocElemType::CAPTION) {
            render_child(child);
        }
    }
    // Render caption at end
    if (has_caption) {
        strbuf_append_str(out, "<figcaption>");
        render_caption();
        strbuf_append_str(out, "</figcaption>\n");
    }
    strbuf_append_str(out, "</figure>\n");
```

### Phase 5: CSS Class Harmonization (Optional)

For maximum LaTeXML compatibility, consider matching their class naming:

| Lambda Class | LaTeXML Class |
|--------------|---------------|
| `latex-document` | `ltx_document` |
| `latex-section` | `ltx_section` |
| `latex-paragraph` | `ltx_para` |

This could be a configuration option in `HtmlOutputOptions`.

## Similarity Score Expectations

After implementing the phases:

| Phase | Expected Similarity |
|-------|-------------------|
| Current (raw format_html) | 0-5% |
| Phase 1 (use unified pipeline) | 20-30% |
| Phase 2 (cross-references) | 35-45% |
| Phase 3 (sectioning) | 50-60% |
| Phase 4 (figures/tables) | 65-75% |
| Phase 5 (CSS classes) | 80-90% |

## Files to Modify

| File | Changes |
|------|---------|
| `test/test_latexml_compare_gtest.cpp` | Use unified pipeline in `latex_to_html()` |
| `lambda/tex/tex_document_model.cpp` | Enhance sectioning, figure handling |
| `lambda/tex/tex_document_model.hpp` | Add caption-related types |
| `lambda/tex/tex_latex_bridge.cpp` | Improve label/ref collection |

## Priority Ranking

1. **Fix test to use unified pipeline** - Immediate (~1 hour)
2. **Cross-reference resolution** - High (~1 week)  
3. **Sectioning wrappers** - High (~3 days)
4. **Figure/table handling** - Medium (~1 week)
5. **CSS class compatibility** - Low (optional)

## Conclusion

The 0% similarity scores are primarily due to the test using the wrong pipeline (`format_html` vs `doc_model_to_html`). The unified pipeline already produces reasonable semantic HTML. Fixing the test infrastructure is the first step, followed by targeted improvements to cross-referencing and document structure handling.
