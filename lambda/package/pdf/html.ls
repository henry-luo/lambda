// pdf/html.ls — Multi-page HTML wrapper
//
// Wraps one or more per-page <svg> elements into a scrollable HTML document.

import util: .util

// ============================================================
// Default stylesheet
// ============================================================

// CSS for the multi-page document. Kept inline so the generated HTML is
// self-contained.
pub DEFAULT_CSS =
    ".pdf-document { background: #f1f3f5; padding: 16px; margin: 0; }\n" ++
    ".pdf-page { display: block; box-sizing: border-box; position: relative; overflow: hidden; " ++
        "max-width: 100%; margin: 0 auto 16px auto; padding: 0; " ++
        "background: #fff; border: 1px solid #d9dee3; " ++
        "box-shadow: 0 2px 10px rgba(15,23,42,0.08); }\n" ++
    ".pdf-page svg { display: block; max-width: 100%; height: auto; background: #fff; position: relative; z-index: 0; }\n" ++
    ".pdf-text-layer { position: absolute; left: 0; top: 0; z-index: 1; overflow: hidden; " ++
        "user-select: text; -webkit-user-select: text; cursor: text; }\n" ++
    ".pdf-text-layer span { position: absolute; display: block; margin: 0; padding: 0; " ++
        "white-space: pre; line-height: 1; color: transparent; background: transparent; " ++
        "user-select: text; -webkit-user-select: text; cursor: text; }\n" ++
    ".pdf-page svg text { user-select: text; -webkit-user-select: text; " ++
        "cursor: text; }\n"

// ============================================================
// Page wrapper
// ============================================================

fn _attr_str(el, key, fallback) {
    let v = el[key]
    if (v == null) { fallback } else { string(v) }
}

fn _attr_num(el, key, fallback) {
    let v = el[key]
    if (v == null) { fallback }
    else if (v is int) { float(v) }
    else if (v is float) { v }
    else { float(string(v)) }
}

fn _text_content(el) {
    if (len(el) == 0) { "" } else { string(el[0]) }
}

fn _ends_with_space(s: string) {
    if (len(s) == 0) { true }
    else {
        let ch = s[len(s) - 1]
        ch == " " or ch == "\t" or ch == "\n" or ch == "\r"
    }
}

fn _copy_content(el) {
    let text = _text_content(el)
    if (_ends_with_space(text)) { text } else { text ++ " " }
}

fn _text_span_style(t) {
    let x = _attr_num(t, "data-pdf-x", _attr_num(t, "x", 0.0))
    let y = _attr_num(t, "data-pdf-y", _attr_num(t, "y", 0.0))
    let size = _attr_num(t, "data-pdf-size", _attr_num(t, "font-size", 12.0))
    let width = _attr_num(t, "data-pdf-width", _attr_num(t, "textLength", 0.0))
    let top = y - size
    let family = _attr_str(t, "font-family", "sans-serif")
    let weight = _attr_str(t, "font-weight", "normal")
    let font_style = _attr_str(t, "font-style", "normal")
    "left:" ++ util.fmt_num(x) ++ "px;top:" ++ util.fmt_num(top) ++ "px;" ++
    "font-family:'" ++ family ++ "';font-size:" ++ util.fmt_num(size) ++ "px;" ++
    "font-weight:" ++ weight ++ ";font-style:" ++ font_style ++ ";" ++
    "width:" ++ util.fmt_num(width) ++ "px;height:" ++ util.fmt_num(size * 1.25) ++ "px;"
}

fn _text_span(t) {
    <span class: "pdf-text-run", style: _text_span_style(t); _copy_content(t)>
}

pub fn text_layer(texts, width, height) {
    let style = "width:" ++ width ++ "px;height:" ++ height ++ "px;"
    <div class: "pdf-text-layer", style: style;
        for (t in texts) _text_span(t)
    >
}

// Wrap a single <svg> element in a <div class="pdf-page" data-page="n">.
pub fn page_div(svg_el, page_num: int) {
    let page_style = "width: " ++ svg_el.width ++ "px;"
    <div class: "pdf-page", 'data-page': string(page_num), style: page_style;
        svg_el
    >
}

pub fn page_div_with_text_layer(svg_el, texts, page_num: int) {
    let page_style = "width: " ++ svg_el.width ++ "px;"
    <div class: "pdf-page", 'data-page': string(page_num), style: page_style;
        svg_el
        text_layer(texts, svg_el.width, svg_el.height)
    >
}

// ============================================================
// Document shell
// ============================================================

pub fn html_shell(svgs, opts) {
    let title = if (opts and opts.title) opts.title else "PDF Document";
    let css = if (opts and opts.css) opts.css else DEFAULT_CSS;
    <html;
        <head;
            <meta charset: "utf-8">
            <title; title>
            <style; css>
        >
        <body class: "pdf-document";
            for (i in 0 to (len(svgs) - 1)) page_div(svgs[i], i + 1)
        >
    >
}

pub fn html_shell_pages(pages, opts) {
    let title = if (opts and opts.title) opts.title else "PDF Document";
    let css = if (opts and opts.css) opts.css else DEFAULT_CSS;
    <html;
        <head;
            <meta charset: "utf-8">
            <title; title>
            <style; css>
        >
        <body class: "pdf-document";
            for (p in pages) p
        >
    >
}
