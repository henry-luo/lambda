// latex/elements/font_decl.ls — Font declaration commands and alignment declarations
// \itshape, \bfseries, \ttfamily, \rmfamily, \sffamily, \scshape, \slshape, \upshape, \mdseries
// \centering, \raggedleft, \raggedright

// ============================================================
// Font declaration style map
// ============================================================

// maps font declaration tag names to CSS style strings
let FONT_DECL_STYLES = {
    'itshape':   "font-style:italic",
    'bfseries':  "font-weight:bold",
    'ttfamily':  "font-family:monospace",
    'rmfamily':  "font-family:serif",
    'sffamily':  "font-family:sans-serif",
    'scshape':   "font-variant:small-caps",
    'slshape':   "font-style:oblique",
    'upshape':   "font-style:normal",
    'mdseries':  "font-weight:normal"
}

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
    FONT_DECL_STYLES[tag_str] != null
}

// check if a tag name is an alignment declaration
pub fn is_align_decl(tag_str) {
    ALIGN_DECL_STYLES[tag_str] != null
}

// get CSS style for a font declaration tag
pub fn font_decl_style(tag_str) {
    FONT_DECL_STYLES[tag_str]
}

// get CSS style for an alignment declaration tag
pub fn align_decl_style(tag_str) {
    ALIGN_DECL_STYLES[tag_str]
}

// wrap rendered items in a span with font declaration style
// callers guarantee tag_str is a valid font declaration key
pub fn wrap_font_decl(tag_str, items) {
    let style = FONT_DECL_STYLES[tag_str]
    <span class: "latex-" ++ tag_str, style: style; for c in items { c }>
}

// wrap rendered items in a div with alignment style
// callers guarantee tag_str is a valid alignment declaration key
pub fn wrap_align_decl(tag_str, items) {
    let style = ALIGN_DECL_STYLES[tag_str]
    <div class: "latex-" ++ tag_str, style: style; for c in items { c }>
}
