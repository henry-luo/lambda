// pdf/html.ls — Multi-page HTML wrapper
//
// Wraps one or more per-page <svg> elements into a scrollable HTML document.

// ============================================================
// Default stylesheet
// ============================================================

// CSS for the multi-page document. Kept inline so the generated HTML is
// self-contained.
pub DEFAULT_CSS =
    ".pdf-document { background: #f1f3f5; padding: 16px; margin: 0; }\n" ++
    ".pdf-page { display: block; box-sizing: border-box; " ++
        "max-width: 100%; margin: 0 auto 16px auto; padding: 0; " ++
        "background: #fff; border: 1px solid #d9dee3; " ++
        "box-shadow: 0 2px 10px rgba(15,23,42,0.08); }\n" ++
    ".pdf-page svg { display: block; max-width: 100%; height: auto; background: #fff; }\n" ++
    ".pdf-page svg text { user-select: text; -webkit-user-select: text; " ++
        "cursor: text; }\n"

// ============================================================
// Page wrapper
// ============================================================

// Wrap a single <svg> element in a <div class="pdf-page" data-page="n">.
pub fn page_div(svg_el, page_num: int) {
    let page_style = "width: " ++ svg_el.width ++ "px;"
    <div class: "pdf-page", 'data-page': string(page_num), style: page_style;
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
