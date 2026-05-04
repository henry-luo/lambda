// pdf/html.ls — Multi-page HTML wrapper
//
// Wraps one or more per-page <svg> elements into a scrollable HTML document.

// ============================================================
// Default stylesheet
// ============================================================

// CSS for the multi-page document. Kept inline so the generated HTML is
// self-contained.
pub DEFAULT_CSS =
    ".pdf-document { background: #525659; padding: 16px 0; margin: 0; }\n" ++
    ".pdf-page { display: block; margin: 8px auto; " ++
        "box-shadow: 0 2px 8px rgba(0,0,0,0.5); background: white; }\n" ++
    ".pdf-page svg { display: block; }\n" ++
    ".pdf-page svg text { user-select: text; -webkit-user-select: text; " ++
        "cursor: text; }\n"

// ============================================================
// Page wrapper
// ============================================================

// Wrap a single <svg> element in a <div class="pdf-page" data-page="n">.
pub fn page_div(svg_el, page_num: int) {
    <div class: "pdf-page", 'data-page': string(page_num);
        svg_el
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
