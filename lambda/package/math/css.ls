// math/css.ls — CSS class names and embedded stylesheet for math rendering
// Based on MathLive's core.less (~810 lines), compiled to essential CSS

// ============================================================
// CSS class name constants
// ============================================================

pub LATEX = "ML__latex"
pub BASE = "ML__base"
pub STRUT = "ML__strut"
pub STRUT_BOTTOM = "ML__strut--bottom"
pub MATHIT = "ML__mathit"
pub CMR = "ML__cmr"
pub MATHBF = "ML__mathbf"
pub AMS = "ML__ams"
pub BB = "ML__bb"
pub CAL = "ML__cal"
pub FRAK = "ML__frak"
pub TT = "ML__tt"
pub SCRIPT = "ML__script"
pub SANS = "ML__sans"
pub TEXT = "ML__text"
pub BOLD = "ML__bold"
pub IT = "ML__it"
pub MFRAC = "ML__mfrac"
pub FRAC_LINE = "ML__frac-line"
pub SQRT = "ML__sqrt"
pub SQRT_SIGN = "ML__sqrt-sign"
pub SQRT_LINE = "ML__sqrt-line"
pub SQRT_INDEX = "ML__sqrt-index"
pub VLIST_T = "ML__vlist-t"
pub VLIST_T2 = "ML__vlist-t ML__vlist-t2"
pub VLIST_R = "ML__vlist-r"
pub VLIST = "ML__vlist"
pub VLIST_S = "ML__vlist-s"
pub PSTRUT = "ML__pstrut"
pub MSUBSUP = "ML__msubsup"
pub LEFT_RIGHT = "ML__left-right"
pub SMALL_DELIM = "ML__small-delim"
pub DELIM_SIZE1 = "ML__delim-size1"
pub DELIM_SIZE2 = "ML__delim-size2"
pub DELIM_SIZE3 = "ML__delim-size3"
pub DELIM_SIZE4 = "ML__delim-size4"
pub ACCENT_BODY = "ML__accent-body"
pub NEG_THIN = "ML__negativethinspace"
pub THIN = "ML__thinspace"
pub MEDIUM = "ML__mediumspace"
pub THICK = "ML__thickspace"
pub ENSPACE = "ML__enspace"
pub QUAD = "ML__quad"
pub QQUAD = "ML__qquad"
pub RULE = "ML__rule"
pub NULLDELIMITER = "ML__nulldelimiter"
pub ERROR = "ML__error"

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
        default: MATHIT
    }
}

// ============================================================
// Embedded stylesheet
// ============================================================

pub fn get_stylesheet() {
    let s = ".ML__latex{display:inline-block;direction:ltr;text-align:left;text-indent:0;text-rendering:auto;font-family:inherit;font-style:normal;letter-spacing:normal;line-height:1.2;word-wrap:normal;word-spacing:normal;white-space:nowrap;width:min-content}" ++
    ".ML__base{display:inline-block;position:relative;padding:0;margin:0;box-sizing:content-box;border:0;outline:0;vertical-align:baseline;text-decoration:none;width:min-content}" ++
    ".ML__strut,.ML__strut--bottom{display:inline-block;min-height:0.5em}" ++
    ".ML__mathit{font-family:KaTeX_Math;font-style:italic}" ++
    ".ML__cmr{font-family:KaTeX_Main;font-style:normal}" ++
    ".ML__mathbf{font-family:KaTeX_Main;font-weight:bold}" ++
    ".ML__ams,.ML__bb{font-family:KaTeX_AMS}" ++
    ".ML__cal{font-family:KaTeX_Caligraphic}" ++
    ".ML__frak{font-family:KaTeX_Fraktur}" ++
    ".ML__tt{font-family:KaTeX_Typewriter}" ++
    ".ML__script{font-family:KaTeX_Script}" ++
    ".ML__sans{font-family:KaTeX_SansSerif}" ++
    ".ML__text{font-family:system-ui,-apple-system,BlinkMacSystemFont,sans-serif;white-space:pre}" ++
    ".ML__bold{font-weight:700}" ++
    ".ML__it{font-style:italic}" ++
    ".ML__mfrac{display:inline-block}" ++
    ".ML__frac-line{width:100%;min-height:1px}" ++
    ".ML__frac-line:after{content:'';display:block;margin-top:max(-1px,-0.04em);min-height:max(1px,0.04em);background:currentColor;box-sizing:content-box;transform:translate(0,0)}" ++
    ".ML__sqrt{display:inline-block}" ++
    ".ML__sqrt-sign{display:inline-block;position:relative}" ++
    ".ML__sqrt-line{display:inline-block;height:max(1px,0.04em);width:100%}" ++
    ".ML__sqrt-line:before{content:'';display:block;margin-top:min(-1px,-0.04em);min-height:max(1px,0.04em);background:currentColor;transform:translate(0,0)}" ++
    ".ML__sqrt-index{margin-left:0.27778em;margin-right:-0.55556em}" ++
    ".ML__latex .ML__vlist-t{display:inline-table;table-layout:fixed;border-collapse:collapse}" ++
    ".ML__latex .ML__vlist-r{display:table-row}" ++
    ".ML__latex .ML__vlist{display:table-cell;vertical-align:bottom;position:relative}" ++
    ".ML__latex .ML__vlist>span{display:block;height:0;position:relative}" ++
    ".ML__latex .ML__vlist>span>span{display:inline-block}" ++
    ".ML__latex .ML__vlist>span>.ML__pstrut{overflow:hidden;width:0}" ++
    ".ML__latex .ML__vlist-t2{margin-right:-2px}" ++
    ".ML__latex .ML__vlist-s{display:table-cell;vertical-align:bottom;font-size:1px;width:2px;min-width:2px}" ++
    ".ML__msubsup{text-align:left}" ++
    ".ML__left-right{display:inline-block}" ++
    ".ML__small-delim{font-family:KaTeX_Main}" ++
    ".ML__delim-size1{font-family:KaTeX_Size1}" ++
    ".ML__delim-size2{font-family:KaTeX_Size2}" ++
    ".ML__delim-size3{font-family:KaTeX_Size3}" ++
    ".ML__delim-size4{font-family:KaTeX_Size4}" ++
    ".ML__accent-body{font-family:KaTeX_Main}" ++
    ".ML__negativethinspace{display:inline-block;margin-left:-0.16667em;height:0.71em}" ++
    ".ML__thinspace{display:inline-block;width:0.16667em;height:0.71em}" ++
    ".ML__mediumspace{display:inline-block;width:0.22222em;height:0.71em}" ++
    ".ML__thickspace{display:inline-block;width:0.27778em;height:0.71em}" ++
    ".ML__enspace{display:inline-block;width:0.5em;height:0.71em}" ++
    ".ML__quad{display:inline-block;width:1em;height:0.71em}" ++
    ".ML__qquad{display:inline-block;width:2em;height:0.71em}" ++
    ".ML__nulldelimiter{display:inline-block;width:0.12em}" ++
    ".ML__rule{display:inline-block;border:solid 0;position:relative;box-sizing:border-box}" ++
    ".ML__error{color:#bc2612}"
    s
}

// wrap output in standalone HTML with style tag
pub fn wrap_standalone(content_el) =>
    <span;
        <style; get_stylesheet()>
        content_el
    >
