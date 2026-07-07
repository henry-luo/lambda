// pdf/pdf.ls — Public API for the Lambda PDF Package
//
// Phase 1 (skeleton): convert a parsed PDF (output of `input(path, 'pdf')`)
// into an HTML document containing one <svg> per page, where each <svg>
// has the correct viewBox + page-number label.
//
// Phase 2: wire the content-stream pipeline:
//   raw bytes → pdf_parse_content_stream → interp.render_page → SVG
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
    let txt = "Page " ++ (page_index + 1)
    svg.text_run(label_xform, "Helvetica", 12.0, "rgb(80,80,80)", txt)
}

// Build label group as a (possibly empty) list.
fn _label_children(rect, page_index, opts) {
    let show = (opts and (opts.show_label == true))
    if (show) { [_label_layer(rect, page_index)] }
    else { [] }
}

// Build the y-flip wrapper holding only PDF-space content (paths).
// The page label sits OUTSIDE the flip group (in SVG space) to avoid
// being mirrored by the y-flip matrix.
fn _flip_group_list(rect, paths) {
    if (len(paths) == 0) { [] }
    else {
        let flip_xform = coords.y_flip_transform(rect.y + rect.h)
        [svg.group(flip_xform, paths)]
    }
}

// Tokenize + interpret a page's content stream. Empty content yields
// { texts: [], paths: [] }.
fn _content_elements(pdf, page, page_h) {
    let raw_bytes = resolve.page_content_bytes(pdf, page)
    if (raw_bytes == null) {
        { texts: [], paths: [] }
    }
    else {
        let fonts = interp.resolve_page_fonts(pdf, page)
        let ops = pdf_parse_content_stream(raw_bytes)
        interp.render_page_with_fonts(pdf, page, ops, page_h, fonts)
    }
}

// Resolve the page background color from opts; pulled into `fn`
// because `let x = if (c) ... else ...` returns null inside `pn`
// (see vibe/Lambda_Issues5.md #15).
fn _resolve_bg(opts) {
    if (opts and opts.background) { opts.background }
    else { "white" }
}

fn _render_page_parts(pdf, page, page_index, opts) {
    let rect = coords.media_box_rect(page)
    let view_box = coords.view_box_attr(page)
    let bg = _resolve_bg(opts)

    let r = _content_elements(pdf, page, rect.h)
    let paths = [for (p in r.paths) p]
    let texts = [for (t in r.texts) t]
    let flip_group = _flip_group_list(rect, paths)
    let label_kids = _label_children(rect, page_index, opts)

    let children = [svg.page_background(rect, bg),
                    for (p in flip_group) p,
                    for (t in texts) t,
                    for (l in label_kids) l]

    { svg: svg.svg_pdf_root(view_box, rect.w, rect.h, children, pdf),
      texts: texts,
      rect: rect }
}

fn render_page(pdf, page, page_index, opts) {
    _render_page_parts(pdf, page, page_index, opts).svg
}

fn render_page_div(pdf, page, page_index, opts) {
    let parts = _render_page_parts(pdf, page, page_index, opts)
    html.page_div_with_text_layer(parts.svg, parts.texts, page_index + 1)
}

fn render_missing(page_index: int) {
    svg.svg_root("0 0 612 792", 612, 792, [
        svg.text_run("matrix(1 0 0 1 50 50)", "Helvetica", 14.0, "rgb(180,0,0)",
                     "Page " ++ (page_index + 1) ++ " not available")
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
pub fn pdf_to_svg(pdf, page_index, opts) {
    let page = resolve.page_at(pdf, page_index)
    if (page == null) { render_missing(page_index) }
    else { render_page(pdf, page, page_index, opts) }
}

// Render the entire document as an HTML shell containing one <svg> per page.
// Optional `opts` map: { title: string, css: string, background: string,
//                        show_label: bool }
fn _render_page_count(total, opts) {
    let max_pages = if (opts and opts.max_pages and opts.max_pages > 0) int(opts.max_pages) else total
    if (max_pages < total) max_pages else total
}

pub fn pdf_to_html(pdf, opts) {
    let n = resolve.page_count(pdf)
    if (n == 0) {
        html.html_shell([render_empty_doc()], opts)
    }
    else {
        let render_count = _render_page_count(n, opts)
        let pages = [for (i in 0 to (render_count - 1)) render_page_div(pdf, resolve.page_at(pdf, i), i, opts)]
        // Inject @font-face rules for embedded fonts so the browser uses
        // the actual glyphs (not OS fallback). Done here — once per doc —
        // rather than per page so the rule appears once.
        let faces = _collect_font_face_rules(pdf, render_count)
        let base_css = _base_css(opts)
        let css = _build_css(faces, base_css)
        let opts2 = _opts_with_css(opts, css)
        html.html_shell_pages(pages, opts2)
    }
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
        let names = [for (k, v in fonts) string(k)];
        names
    }
}

// Build a single @font-face CSS rule.
fn _font_face_rule(family: string, fmt: string, uri: string, weight, style) {
    let face_weight = if (weight == null) "normal" else weight
    let face_style = if (style == null) "normal" else style
    "@font-face { font-family: '" ++ family ++ "'; " ++
        "src: url(\"" ++ uri ++ "\") format('" ++ fmt ++ "'); " ++
        "font-weight: " ++ face_weight ++ "; font-style: " ++ face_style ++ "; " ++
        "font-display: block; }\n"
}

fn _collect_one_page_loop(pdf, page, names, i, seen_families, rules) {
    if (i >= len(names)) { { seen: seen_families, rules: rules } }
    else {
        let info = font.resolve_font(pdf, page, names[i])
        let fam = info.embedded_family
        if (fam != null and not contains(seen_families, fam)) {
            _collect_one_page_loop(pdf, page, names, i + 1,
                seen_families ++ [fam],
                rules ++ [_font_face_rule(fam, info.font_format, info.font_data_uri, info.weight, info.style)])
        }
        else { _collect_one_page_loop(pdf, page, names, i + 1, seen_families, rules) }
    }
}

fn _collect_one_page(pdf, page, seen_families, rules) {
    _collect_one_page_loop(pdf, page, _page_font_names(pdf, page), 0, seen_families, rules)
}

fn _collect_font_face_loop(pdf, i, n, seen, rules) {
    if (i >= n) { rules }
    else {
        let page = resolve.page_at(pdf, i)
        let r = _collect_one_page(pdf, page, seen, rules)
        _collect_font_face_loop(pdf, i + 1, n, r.seen, r.rules)
    }
}

fn _collect_font_face_rules(pdf, limit) {
    let total = resolve.page_count(pdf)
    let n = _bounded_count(total, limit)
    _collect_font_face_loop(pdf, 0, n, [], []) |> join("")
}

fn _bounded_count(total, limit) {
    if (limit < total) limit else total
}

// Build the final stylesheet: base page chrome first, then @font-face rules.
fn _build_css(face_rules: string, base_css: string) {
    if (face_rules == "") { base_css }
    else { base_css ++ face_rules }
}

fn _opts_with_css(opts, css: string) {
    if (opts == null) { { css: css } }
    else              { { *: opts, css: css } }
}

fn _base_css(opts) {
    if (opts and opts.css) { opts.css }
    else { html.DEFAULT_CSS }
}
