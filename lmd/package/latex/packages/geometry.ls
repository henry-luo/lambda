// Fixed-page CSS approximation. Radiant does not yet consume @page size/margins for PDF.
import util: ~~.util

let DIMENSION_KEYS = ["margin", "left", "right", "top", "bottom", "inner", "outer",
    "paperwidth", "paperheight"]

fn invalid_dimension(raw) {
    let css = util.css_dimension(raw)
    if (css == null) true
    else ends_with(css, "%") or ends_with(css, "em") or ends_with(css, "ex")
}

pub fn invalid_options(opts) {
    let dimensions = [for (key in DIMENSION_KEYS
        where opts[key] != null and invalid_dimension(opts[key])) key]
    let papers = [for (key in ["a4paper", "a5paper", "letterpaper", "legalpaper"]
        where util.option_enabled(opts[key])) key]
    dimensions ++
        (if (len(papers) > 1) ["paper"] else []) ++
        (if (util.option_enabled(opts.landscape) and util.option_enabled(opts.portrait))
            ["orientation"] else [])
}

fn paper_size(opts) {
    if (util.option_enabled(opts.a5paper)) ["148mm", "210mm"]
    else if (util.option_enabled(opts.letterpaper)) ["8.5in", "11in"]
    else if (util.option_enabled(opts.legalpaper)) ["8.5in", "14in"]
    else ["210mm", "297mm"]
}

pub fn stylesheet(opts) {
    if (opts == null) ""
    else if (len(invalid_options(opts)) > 0) ""
    else {
        let paper = paper_size(opts)
        let width = if (opts.paperwidth != null) util.css_dimension(opts.paperwidth) else paper[0]
        let height = if (opts.paperheight != null) util.css_dimension(opts.paperheight) else paper[1]
        let page_width = if (util.option_enabled(opts.landscape)) height else width
        let page_height = if (util.option_enabled(opts.landscape)) width else height
        let size = page_width ++ " " ++ page_height
        let margin = if (opts.margin != null) util.css_dimension(opts.margin) else "25mm"
        let left = if (opts.left != null) util.css_dimension(opts.left)
            else if (opts.inner != null) util.css_dimension(opts.inner) else margin
        let right = if (opts.right != null) util.css_dimension(opts.right)
            else if (opts.outer != null) util.css_dimension(opts.outer) else margin
        let top = if (opts.top != null) util.css_dimension(opts.top) else margin
        let bottom = if (opts.bottom != null) util.css_dimension(opts.bottom) else margin
        "@page{size:" ++ size ++ ";margin:" ++ top ++ " " ++ right ++ " " ++ bottom ++ " " ++ left ++ ";}\n" ++
        ".latex-document{box-sizing:border-box;width:" ++ page_width ++ ";max-width:" ++ page_width ++
        ";padding:" ++ top ++ " " ++ right ++ " " ++ bottom ++ " " ++ left ++ ";}\n"
    }
}

pub fn output_diagnostics(opts, target) {
    if (opts != null and target == "pdf")
        [util.diagnostic("unsupported-page-export", "geometry", "@page",
          "PDF export does not apply @page size or margins", null)]
    else []
}
