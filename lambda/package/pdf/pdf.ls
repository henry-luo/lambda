// pdf/pdf.ls — Public API for the Lambda PDF Package
//
// Phase 1 (skeleton): convert a parsed PDF (output of `input(path, 'pdf')`)
// into an HTML document containing one <svg> per page, where each <svg>
// has the correct viewBox + page-number label.
//
// Phase 2: wire the content-stream pipeline:
//   raw bytes → stream.parse_content_stream → interp.render_page → SVG
// to render real text from each page's content stream.
//
//   input(path, 'pdf')           → Map { version, objects, pages, … }
//   pdf.pdf_to_svg(pdf, 0)       → <svg> for page 0
//   pdf.pdf_to_html(pdf)         → <html> with one <svg> per page

import util:    .util
import coords:  .coords
import svg:     .svg
import html:    .html
import resolve: .resolve
import stream:  .stream
import interp:  .interp
import font:    .font

// ============================================================
// Phase 2 page rendering
// ============================================================
//
// Layout strategy:
//   - Page background drawn in SVG space.
//   - The y-flip wrapper currently holds only the page-number label
//     (still authored in PDF user space). The label is kept for
//     debugging visibility; opts.show_label = false suppresses it.
//   - Text elements emitted by interp.render_page are already in SVG
//     space (text.ls computes y = page_h - Tm[5]) so they sit as
//     siblings of the flipped group, NOT inside it.

fn _label_layer(rect, page_index) {
    // Render the label directly in SVG space so it isn't affected by
    // the page-level y-flip (which would otherwise render it upside
    // down). x/y measured from the SVG origin (top-left).
    let label_x = rect.x + 12.0
    let label_y = rect.y + 18.0
    let label_xform = ("matrix(1 0 0 1 " ++ util.fmt_num(label_x)
                       ++ " " ++ util.fmt_num(label_y) ++ ")")
    let txt = "Page " ++ string(page_index + 1)
    svg.text_run(label_xform, "Helvetica", 12.0, "rgb(80,80,80)", txt)
}

// Build label group as a (possibly empty) list. Done in `fn` because
// `let x = if (c) [...] else [...]` returns null inside `pn`
// (vibe/Lambda_Issues5.md #15).
pn _label_children(rect, page_index, opts) {
    let suppress = (opts and (opts.show_label == false))
    var out = []
    if (suppress) { out = [] }
    else { out = [_label_layer(rect, page_index)] }
    return out
}

// Build the y-flip wrapper holding only PDF-space content (paths).
// The page label sits OUTSIDE the flip group (in SVG space) to avoid
// being mirrored by the y-flip matrix.
pn _flip_group_list(rect, paths) {
    var out = []
    if (len(paths) == 0) { out = [] }
    else {
        let flip_xform = coords.y_flip_transform(rect.y + rect.h)
        out = [svg.group(flip_xform, paths)]
    }
    return out
}

// Tokenize + interpret a page's content stream. Empty content yields
// { texts: [], paths: [] }.
pn _content_elements(pdf, page, page_h) {
    let raw_bytes = resolve.page_content_bytes(pdf, page)
    if (raw_bytes == null) {
        return { texts: [], paths: [] }
    }
    let bytes = interp.expand_forms_in_bytes(pdf, page, raw_bytes)
    let ops = stream.parse_content_stream(bytes)
    return interp.render_page(pdf, page, ops, page_h)
}

// Resolve the page background color from opts; pulled into `fn`
// because `let x = if (c) ... else ...` returns null inside `pn`
// (see vibe/Lambda_Issues5.md #15).
fn _resolve_bg(opts) {
    if (opts and opts.background) { opts.background }
    else { "white" }
}

pn render_page(pdf, page, page_index, opts) {
    let rect = coords.media_box_rect(page)
    let view_box = coords.view_box_attr(page)
    let bg = _resolve_bg(opts)

    let r = _content_elements(pdf, page, rect.h)
    let paths = (for (p in r.paths) p)
    let texts = (for (t in r.texts) t)
    let flip_group = _flip_group_list(rect, paths)
    let label_kids = _label_children(rect, page_index, opts)

    let children = [svg.page_background(rect, bg)] ++ flip_group ++ texts ++ label_kids

    return svg.svg_root(view_box, rect.w, rect.h, children)
}

fn render_missing(page_index: int) {
    svg.svg_root("0 0 612 792", 612, 792, [
        svg.text_run("matrix(1 0 0 1 50 50)", "Helvetica", 14.0, "rgb(180,0,0)",
                     "Page " ++ string(page_index + 1) ++ " not available")
    ])
}

fn render_empty_doc() {
    svg.svg_root("0 0 612 200", 612, 200, [
        svg.text_run("matrix(1 0 0 1 50 100)", "Helvetica", 14.0, "rgb(180,0,0)",
                     "Empty PDF (no pages)")
    ])
}

// ============================================================
// Public API
// ============================================================

// Render a single page as a standalone <svg> element. `page_index` is 0-based.
pub pn pdf_to_svg(pdf, page_index, opts) {
    let page = resolve.page_at(pdf, page_index)
    if (page == null) { return render_missing(page_index) }
    return render_page(pdf, page, page_index, opts)
}

// Render the entire document as an HTML shell containing one <svg> per page.
// Optional `opts` map: { title: string, css: string, background: string,
//                        show_label: bool }
pub pn pdf_to_html(pdf, opts) {
    let n = resolve.page_count(pdf)
    if (n == 0) {
        return html.html_shell([render_empty_doc()], opts)
    }
    // While loop because `for` comprehensions cannot call `pn` helpers
    // without dragging the whole expression into a pn context (and even
    // then the body must be expression-shaped). Use a plain driver loop
    // for clarity and to keep behaviour predictable.
    var svgs = []
    var i = 0
    while (i < n) {
        let page = resolve.page_at(pdf, i)
        let s = render_page(pdf, page, i, opts)
        svgs = svgs ++ [s]
        i = i + 1
    }
    // Inject @font-face rules for embedded fonts so the browser uses
    // the actual glyphs (not OS fallback). Done here — once per doc —
    // rather than per page so the rule appears once.
    let faces = _collect_font_face_rules(pdf)
    let base_css = if (opts and opts.css) opts.css else html.DEFAULT_CSS
    let css = _build_css(faces, base_css)
    let opts2 = _opts_with_css(opts, css)
    return html.html_shell(svgs, opts2)
}

// Number of pages in the document.
pub fn pdf_page_count(pdf) {
    resolve.page_count(pdf)
}

// Document metadata from the trailer's /Info dict (best-effort).
pub fn pdf_metadata(pdf) {
    resolve.metadata(pdf)
}

// ============================================================
// Embedded font collection (@font-face)
// ============================================================
//
// Walk every font referenced by every page; for those whose program
// the C-side post-processor extracted (font_data_uri set), emit a
// `@font-face` rule keyed on the unsubsetted family name. Browsers
// then load the actual glyphs instead of falling back to an OS font
// with different metrics.

// Locate font names in a page's /Resources/Font sub-dict. Returns a
// list of the keys (e.g. ["F1", "F2", "F3"]). Empty when the page has
// no font resources.
fn _page_font_names(pdf, page) {
    let res = resolve.page_resources(pdf, page)
    let fonts = if (res and res.Font) resolve.deref(pdf, res.Font) else null
    if (fonts == null) { [] }
    else {
        let names = for (k, v in fonts) string(k);
        names
    }
}

// Build a single @font-face CSS rule.
fn _font_face_rule(family: string, fmt: string, uri: string) {
    "@font-face { font-family: '" ++ family ++ "'; " ++
        "src: url(\"" ++ uri ++ "\") format('" ++ fmt ++ "'); " ++
        "font-weight: normal; font-style: normal; " ++
        "font-display: block; }\n"
}

pn _collect_one_page(pdf, page, seen_families, rules) {
    var s = seen_families
    var r = rules
    let names = _page_font_names(pdf, page)
    var i = 0
    let n = len(names)
    while (i < n) {
        let info = font.resolve_font(pdf, page, names[i])
        let fam = info.embedded_family
        if (fam != null and not contains(s, fam)) {
            s = s ++ [fam]
            r = r ++ [_font_face_rule(fam, info.font_format,
                                       info.font_data_uri)]
        }
        i = i + 1
    }
    return { seen: s, rules: r }
}

pn _collect_font_face_rules(pdf) {
    let n = resolve.page_count(pdf)
    var seen = []
    var rules = []
    var i = 0
    while (i < n) {
        let page = resolve.page_at(pdf, i)
        let r = _collect_one_page(pdf, page, seen, rules)
        seen = r.seen
        rules = r.rules
        i = i + 1
    }
    return rules | join("")
}

// Build the final stylesheet: @font-face block first, then base CSS.
fn _build_css(face_rules: string, base_css: string) {
    if (face_rules == "") { base_css }
    else { face_rules ++ base_css }
}

fn _opts_with_css(opts, css: string) {
    if (opts == null) { { css: css } }
    else              { { *: opts, css: css } }
}
