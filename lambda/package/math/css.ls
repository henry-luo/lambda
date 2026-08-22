// math/css.ls — CSS class names and embedded stylesheet for math rendering
// Based on MathLive's core.less (~810 lines), compiled to essential CSS

// ============================================================
// CSS class name constants
// ============================================================

pub let LATEX = "lm_latex"
pub let BASE = "lm_base"
pub let STRUT = "lm_strut"
pub let STRUT_BOTTOM = "lm_strut--bottom"
pub let MATHIT = "lm_mathit"
pub let CMR = "lm_cmr"
pub let MATHBF = "lm_mathbf"
pub let AMS = "lm_ams"
pub let BB = "lm_bb"
pub let CAL = "lm_cal"
pub let FRAK = "lm_frak"
pub let TT = "lm_tt"
pub let SCRIPT = "lm_script"
pub let SANS = "lm_sans"
pub let TEXT = "lm_text"
pub let BOLD = "lm_bold"
pub let IT = "lm_it"
pub let MFRAC = "lm_mfrac"
pub let FRAC_LINE = "lm_frac-line"
pub let SQRT = "lm_sqrt"
pub let SQRT_SIGN = "lm_sqrt-sign"
pub let SQRT_LINE = "lm_sqrt-line"
pub let SQRT_INDEX = "lm_sqrt-index"
pub let VLIST_T = "lm_vlist-t"
pub let VLIST_T2 = "lm_vlist-t lm_vlist-t2"
pub let VLIST_R = "lm_vlist-r"
pub let VLIST = "lm_vlist"
pub let VLIST_S = "lm_vlist-s"
pub let PSTRUT = "lm_pstrut"
pub let CENTER = "lm_center"
pub let MSUBSUP = "lm_msubsup"
pub let LEFT_RIGHT = "lm_left-right"
pub let SMALL_DELIM = "lm_small-delim"
pub let OPEN = "lm_open"
pub let CLOSE = "lm_close"
pub let DELIM_SIZE1 = "lm_delim-size1"
pub let DELIM_SIZE2 = "lm_delim-size2"
pub let DELIM_SIZE3 = "lm_delim-size3"
pub let DELIM_SIZE4 = "lm_delim-size4"
pub let ACCENT_BODY = "lm_accent-body"
pub let NEG_THIN = "lm_negativethinspace"
pub let THIN = "lm_thinspace"
pub let MEDIUM = "lm_mediumspace"
pub let THICK = "lm_thickspace"
pub let ENSPACE = "lm_enspace"
pub let QUAD = "lm_quad"
pub let QQUAD = "lm_qquad"
pub let RULE = "lm_rule"
pub let NULLDELIMITER = "lm_nulldelimiter"
pub let MTABLE = "lm_mtable"
pub let ALIGN_ENVIRONMENT = "lm_align_environment"
pub let ERROR = "lm_error"
pub let OP_GROUP = "lm_op-group"
pub let BG = "lm_bg"

// ============================================================
// Class builder helpers
// ============================================================

// join multiple class names (skip nulls)
pub fn classes(parts) {
    let filtered = (for (p in parts where p != null and p != "") p)
    if (len(filtered) == 0) null
    else join_classes(filtered, 1, filtered[0])
}

fn join_classes(filtered, i, acc) {
    if (i >= len(filtered)) acc
    else join_classes(filtered, i + 1, acc ++ " " ++ filtered[i])
}

// font class for a given font name
pub fn font_class(font_name) {
    match font_name {
        case "mathit": MATHIT
        case "cmr": CMR
        case "mathbf": MATHBF
        case "ams": AMS
        case "bb": BB
        case "cal": CAL
        case "frak": FRAK
        case "tt": TT
        case "script": SCRIPT
        case "sans": SANS
        case "text": TEXT
        case "it": "lm_cmr lm_it"
        default: MATHIT
    }
}

// ============================================================
// Embedded stylesheet
// ============================================================

pub fn get_stylesheet(options = null) {
    let families = font_families(options)
    let s = ".lm_latex{display:inline-block;direction:ltr;text-align:left;text-indent:0;text-rendering:auto;font-family:inherit;font-style:normal;letter-spacing:normal;line-height:1.2;word-wrap:normal;word-spacing:normal;white-space:nowrap}" ++
    ".lm_base{display:inline-block;position:relative;padding:0;margin:0;box-sizing:content-box;border:0;outline:0;vertical-align:baseline;text-decoration:none}" ++
    ".lm_strut,.lm_strut--bottom{display:inline-block;min-height:0.5em}" ++
    ".lm_mathit{font-family:" ++ families.math ++ ";font-style:italic}" ++
    ".lm_cmr{font-family:" ++ families.main ++ ";font-style:normal}" ++
    ".lm_mathbf{font-family:" ++ families.main ++ ";font-weight:bold}" ++
    ".lm_ams,.lm_bb{font-family:" ++ families.ams ++ "}" ++
    ".lm_cal{font-family:" ++ families.cal ++ "}" ++
    ".lm_frak{font-family:" ++ families.frak ++ "}" ++
    ".lm_tt{font-family:" ++ families.tt ++ "}" ++
    ".lm_script{font-family:" ++ families.script ++ "}" ++
    ".lm_sans{font-family:" ++ families.sans ++ "}" ++
    ".lm_text{font-family:system-ui,-apple-system,BlinkMacSystemFont,sans-serif;white-space:pre}" ++
    ".lm_op-group{display:inline-block}" ++
    ".lm_bold{font-weight:700}" ++
    ".lm_it{font-style:italic}" ++
    ".lm_mfrac{display:inline-block}" ++
    ".lm_frac-line{display:block;height:1px;min-height:1px;background:currentColor;margin:0.1em 0}" ++
    ".lm_sqrt{display:inline-block}" ++
    ".lm_sqrt-sign{display:inline-block;position:relative}" ++
    ".lm_sqrt-line{display:inline-block;height:1px;width:100%;background:currentColor}" ++
    ".lm_sqrt-index{margin-left:0.27778em;margin-right:-0.55556em}" ++
    ".lm_msubsup{text-align:left}" ++
    ".lm_left-right{display:inline-block}" ++
    ".lm_small-delim{font-family:" ++ families.main ++ "}" ++
    ".lm_delim-size1{font-family:" ++ families.size1 ++ "}" ++
    ".lm_delim-size2{font-family:" ++ families.size2 ++ "}" ++
    ".lm_delim-size3{font-family:" ++ families.size3 ++ "}" ++
    ".lm_delim-size4{font-family:" ++ families.size4 ++ "}" ++
    ".lm_accent-body{font-family:" ++ families.main ++ "}" ++
    ".lm_negativethinspace{display:inline-block;margin-left:-0.16667em;height:0.71em}" ++
    ".lm_thinspace{display:inline-block;width:0.16667em;height:0.71em}" ++
    ".lm_mediumspace{display:inline-block;width:0.22222em;height:0.71em}" ++
    ".lm_thickspace{display:inline-block;width:0.27778em;height:0.71em}" ++
    ".lm_enspace{display:inline-block;width:0.5em;height:0.71em}" ++
    ".lm_quad{display:inline-block;width:1em;height:0.71em}" ++
    ".lm_qquad{display:inline-block;width:2em;height:0.71em}" ++
    ".lm_nulldelimiter{display:inline-block;width:0.12em}" ++
    ".lm_rule{display:inline-block;border:solid 0;position:relative;box-sizing:border-box}" ++
    ".lm_mtable{display:inline-flex;flex-direction:column;vertical-align:middle}" ++
    ".lm_error{color:#bc2612}"
    s
}

fn font_option(options) {
    if (options == null) "default"
    else if (options is string) options
    else if (options.font_option != null) string(options.font_option)
    else if (options.math_font != null) string(options.math_font)
    else if (options.font != null) string(options.font)
    else "default"
}

fn use_katex_fonts(options) =>
    font_option(options) == "katex"

fn font_families(options) {
    if (use_katex_fonts(options)) katex_font_families()
    else local_font_families()
}

fn katex_font_families() => {
    main: "KaTeX_Main",
    math: "KaTeX_Math",
    ams: "KaTeX_AMS",
    cal: "KaTeX_Caligraphic",
    frak: "KaTeX_Fraktur",
    tt: "KaTeX_Typewriter",
    script: "KaTeX_Script",
    sans: "KaTeX_SansSerif",
    size1: "KaTeX_Size1",
    size2: "KaTeX_Size2",
    size3: "KaTeX_Size3",
    size4: "KaTeX_Size4"
}

fn local_font_families() => {
    main: "'Computer Modern Serif','Latin Modern Roman',KaTeX_Main,serif",
    math: "'Computer Modern Serif','Latin Modern Roman',KaTeX_Math,serif",
    ams: "KaTeX_AMS,'Computer Modern Serif','Latin Modern Roman',serif",
    cal: "KaTeX_Caligraphic,'Computer Modern Serif','Latin Modern Roman',serif",
    frak: "KaTeX_Fraktur,'Computer Modern Serif','Latin Modern Roman',serif",
    tt: "'Computer Modern Typewriter','Latin Modern Mono',KaTeX_Typewriter,monospace",
    script: "KaTeX_Script,'Computer Modern Serif','Latin Modern Roman',serif",
    sans: "'Computer Modern Sans','Latin Modern Sans',KaTeX_SansSerif,sans-serif",
    size1: "KaTeX_Size1",
    size2: "KaTeX_Size2",
    size3: "KaTeX_Size3",
    size4: "KaTeX_Size4"
}

// wrap output in standalone HTML with style tag
pub fn wrap_standalone(content_el, options = null) =>
    <span;
        <style; get_stylesheet(options)>
        content_el
    >
