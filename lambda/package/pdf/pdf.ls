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
    let label_x = rect.x + 12.0
    let label_y = rect.y + rect.h - 18.0
    let label_xform = ("matrix(1 0 0 1 " ++ util.fmt_num(label_x)
                       ++ " " ++ util.fmt_num(label_y) ++ ")")
    let txt = "Page " ++ string(page_index + 1)
    svg.text_run(label_xform, "Helvetica", 12.0, "rgb(80,80,80)", txt)
}

// Build label group as a (possibly empty) list. Done in `fn` because
// `let x = if (c) [...] else [...]` returns null inside `pn`
// (vibe/Lambda_Issues5.md #15).
fn _label_group_list(rect, page_index, opts) {
    let suppress = (opts and (opts.show_label == false))
    if (suppress) { [] }
    else {
        let flip_xform = coords.y_flip_transform(rect.y + rect.h)
        [svg.group(flip_xform, [_label_layer(rect, page_index)])]
    }
}

// Tokenize + interpret a page's content stream. Empty content yields [].
pn _content_elements(pdf, page, page_h) {
    let bytes = resolve.page_content_bytes(pdf, page)
    if (bytes == null) {
        return []
    }
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

    let elements = _content_elements(pdf, page, rect.h)
    let label_group = _label_group_list(rect, page_index, opts)

    let children = [svg.page_background(rect, bg)] ++ label_group ++ elements

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
    return html.html_shell(svgs, opts)
}

// Number of pages in the document.
pub fn pdf_page_count(pdf) {
    resolve.page_count(pdf)
}

// Document metadata from the trailer's /Info dict (best-effort).
pub fn pdf_metadata(pdf) {
    resolve.metadata(pdf)
}
