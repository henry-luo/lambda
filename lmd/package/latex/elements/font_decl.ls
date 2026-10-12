// latex/elements/font_decl.ls — Font declaration commands and alignment declarations
// \itshape, \bfseries, \ttfamily, \rmfamily, \sffamily, \scshape, \slshape, \upshape, \mdseries
// \centering, \raggedleft, \raggedright
// NFSS: \fontfamily, \fontseries, \fontshape, \usefont, \linespread

import util: lambda.latex.util

// ============================================================
// Font declaration style map
// ============================================================

// maps font declaration tag names to CSS style strings
let FONT_DECL_STYLES = {
    'itshape':   "font-style:italic",
    'bfseries':  "font-weight:bold",
    'bf':        "font-weight:bold",
    'it':        "font-style:italic",
    'rm':        "font-family:serif",
    'tt':        "font-family:monospace",
    'ttfamily':  "font-family:monospace",
    'rmfamily':  "font-family:serif",
    'sffamily':  "font-family:sans-serif",
    'scshape':   "font-variant:small-caps",
    'slshape':   "font-style:oblique",
    'upshape':   "font-style:normal",
    'mdseries':  "font-weight:normal",
    'normalfont': "font-style:normal;font-weight:normal;font-variant:normal"
}

// NFSS family codes. The CM and LM faces are bundled; the others name the
// faces their TeX fonts clone, so the request falls back to installed fonts.
let NFSS_FAMILIES = {
    cmr: "'Computer Modern Serif','Latin Modern Roman',serif",
    lmr: "'Computer Modern Serif','Latin Modern Roman',serif",
    cmss: "'Computer Modern Sans','Latin Modern Sans',sans-serif",
    lmss: "'Computer Modern Sans','Latin Modern Sans',sans-serif",
    cmtt: "'Computer Modern Typewriter','Latin Modern Mono',monospace",
    lmtt: "'Computer Modern Typewriter','Latin Modern Mono',monospace",
    ptm: "'Times New Roman',Times,serif",
    phv: "Helvetica,Arial,sans-serif",
    pcr: "'Courier New',Courier,monospace",
    ppl: "Palatino,'Palatino Linotype','Book Antiqua',serif",
    pbk: "'Bookman Old Style',Bookman,serif",
    pag: "'Century Gothic','Avant Garde',sans-serif",
    pnc: "'Century Schoolbook','New Century Schoolbook',serif",
    bch: "Charter,'Bitstream Charter',serif"
}
let BUNDLED_FAMILIES = ["cmr", "lmr", "cmss", "lmss", "cmtt", "lmtt"]
let NFSS_SERIES = {m: "font-weight:normal", l: "font-weight:300", sb: "font-weight:600",
    b: "font-weight:bold", bx: "font-weight:bold", eb: "font-weight:800"}
let NFSS_SHAPES = {n: "font-style:normal", up: "font-style:normal", it: "font-style:italic",
    sl: "font-style:oblique", sc: "font-variant:small-caps"}
let NFSS_DECLS = ["fontfamily", "fontseries", "fontshape", "usefont", "linespread"]

// alignment declaration tag names to CSS style strings
let ALIGN_DECL_STYLES = {
    'centering':   "text-align:center",
    'raggedright': "text-align:left",
    'raggedleft':  "text-align:right"
}

// ============================================================
// Public API
// ============================================================

// check if a tag name is a font declaration
pub fn is_font_decl(tag_str) {
    FONT_DECL_STYLES[tag_str] != null or contains(NFSS_DECLS, tag_str)
}

// get CSS style for a fixed font switch tag
pub fn font_decl_style(tag_str) {
    FONT_DECL_STYLES[tag_str]
}

fn arg_text(el, i) {
    let raw = util.raw_argument(el, "required", i)
    if (raw == null) null else trim(raw)
}

fn nfss_part(table, key) => if (key == null) null else table[key]

// The CSS of a font declaration element: a fixed switch, or an NFSS command
// read from its arguments. Null for a value outside the tables.
pub fn decl_css(el) {
    let tag = string(name(el))
    if (FONT_DECL_STYLES[tag] != null) font_decl_style(tag)
    else if (tag == "fontfamily") {
        let family = nfss_part(NFSS_FAMILIES, arg_text(el, 0))
        if (family == null) null else "font-family:" ++ family
    }
    else if (tag == "fontseries") nfss_part(NFSS_SERIES, arg_text(el, 0))
    else if (tag == "fontshape") nfss_part(NFSS_SHAPES, arg_text(el, 0))
    else if (tag == "usefont") {
        // \usefont{encoding}{family}{series}{shape}
        let family = nfss_part(NFSS_FAMILIES, arg_text(el, 1))
        let series = nfss_part(NFSS_SERIES, arg_text(el, 2))
        let shape = nfss_part(NFSS_SHAPES, arg_text(el, 3))
        if (family == null or series == null or shape == null) null
        else "font-family:" ++ family ++ ";" ++ series ++ ";" ++ shape
    }
    else if (tag == "linespread") {
        let factor = float(arg_text(el, 0)) ^ { null }
        if (factor == null or factor <= 0.0) null
        else "line-height:" ++ string(line_height_for(factor))
    }
    else null
}

// LaTeX's baselineskip is 1.2 times the font size; \linespread scales it
pub fn line_height_for(factor) => 1.2 * factor

// Family requests for faces Lambda does not ship, as font-substitution diagnostics.
pub fn substitution_issues(node) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let code = if (tag == "fontfamily") arg_text(node, 0)
            else if (tag == "usefont") arg_text(node, 1) else null
        let own = if (code != null and NFSS_FAMILIES[code] != null and
            not contains(BUNDLED_FAMILIES, code))
            [util.diagnostic("font-substitution", "latex", code,
                "NFSS family " ++ code ++ " is not bundled; using an installed equivalent",
                node.source_offset)] else [];
        own ++ [for (child in node, issue in substitution_issues(child)) issue]
    }
}

// check if a tag name is an alignment declaration
pub fn is_align_decl(tag_str) {
    ALIGN_DECL_STYLES[tag_str] != null
}

// get CSS style for an alignment declaration tag
pub fn align_decl_style(tag_str) {
    ALIGN_DECL_STYLES[tag_str]
}

// wrap rendered items in a span with the declaration's style; a declaration
// whose value is outside the tables is reported in place
pub fn wrap_font_decl(decl, items) {
    let tag_str = string(name(decl))
    let style = decl_css(decl)
    if (style == null) [util.unsupported_element("latex",
        "Unsupported font declaration value: \\" ++ tag_str, decl.source_offset), *items]
    else <span class: "latex-" ++ tag_str, style: style, for c in items { c }>
}

// wrap rendered items in a div with alignment style
// callers guarantee tag_str is a valid alignment declaration key
pub fn wrap_align_decl(tag_str, items) {
    let style = ALIGN_DECL_STYLES[tag_str];
    <div class: "latex-" ++ tag_str, style: style, for c in items { c }>
}
