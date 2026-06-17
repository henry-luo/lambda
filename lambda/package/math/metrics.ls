// math/metrics.ls — TeX font metric parameters
// Values from MathLive font-metrics.ts (sourced from TeX's cmsy10/cmsy7/cmsy5)
// Each value is an array [textstyle, scriptstyle, scriptscriptstyle]

import metrics_data: .metrics_data

// ============================================================
// Core constants
// ============================================================

pub AXIS_HEIGHT = 0.25
pub X_HEIGHT = 0.431
pub PT_PER_EM = 10.0
pub BASELINE_SKIP = 1.2

// ============================================================
// Sigma parameters (TeX FONTDIMEN values, in em)
// Index: 0 = textstyle, 1 = scriptstyle, 2 = scriptscriptstyle
// ============================================================

// numerator shifts
pub num1 = [0.5, 0.732, 0.925]         // display style
pub num2 = [0.394, 0.384, 0.5]         // text style (with bar)
pub num3 = [0.444, 0.471, 0.504]       // text style (no bar)

// denominator shifts
pub denom1 = [0.686, 0.752, 1.025]     // display style
pub denom2 = [0.345, 0.344, 0.532]     // text style

// superscript shifts
pub sup1 = [0.413, 0.503, 0.504]       // display style
pub sup2 = [0.363, 0.431, 0.404]       // text (not cramped)
pub sup3 = [0.289, 0.286, 0.294]       // cramped

// subscript shifts
pub sub1 = [0.15, 0.143, 0.2]          // normal
pub sub2 = [0.247, 0.286, 0.4]         // with superscript

// baseline drop for scripts relative to base
pub supDrop = [0.386, 0.353, 0.494]
pub subDrop = [0.05, 0.071, 0.1]

// delimiter sizes
pub delim1 = [2.39, 1.7, 1.98]         // display
pub delim2 = [1.01, 1.157, 1.42]       // text

// rule thickness
pub defaultRuleThickness = [0.04, 0.049, 0.049]
pub sqrtRuleThickness = [0.04, 0.04, 0.04]

// big operator spacing
pub bigOpSpacing1 = [0.111, 0.111, 0.111]  // above gap
pub bigOpSpacing2 = [0.166, 0.166, 0.166]  // below gap
pub bigOpSpacing3 = [0.2, 0.2, 0.2]        // above clearance
pub bigOpSpacing4 = [0.6, 0.611, 0.611]    // below clearance
pub bigOpSpacing5 = [0.1, 0.143, 0.143]    // padding

// quad (1em of font)
pub quad = [1.0, 1.171, 1.472]

// ============================================================
// Font scale table (size index 1..10 → em scale factor)
// Default size = index 5 (1.0)
// ============================================================

pub FONT_SCALE = [0.0, 0.5, 0.7, 0.8, 0.9, 1.0, 1.2, 1.44, 1.728, 2.074, 2.488]
pub DEFAULT_FONT_SIZE = 5

// ============================================================
// Math style → metric index mapping
// ============================================================

// 0 = text/display, 1 = script, 2 = scriptscript
pub fn style_index(style) {
    if (style == "display") 0
    else if (style == "text") 0
    else if (style == "script") 1
    else if (style == "scriptscript") 2
    else 0
}

// math style scale factors
pub fn style_scale(style) {
    if (style == "display") 1.0
    else if (style == "text") 1.0
    else if (style == "script") 0.7
    else if (style == "scriptscript") 0.5
    else 1.0
}

// get metric value for a given style
pub fn get_metric(metric_arr, style) => metric_arr[style_index(style)]

// generic indexed array access (workaround for cross-module array indexing)
pub fn at(arr, i) => arr[i]

// ============================================================
// Default character metrics (fallback)
// ============================================================

pub DEFAULT_CHAR_HEIGHT = 0.7
pub DEFAULT_CHAR_DEPTH = 0.2
pub DEFAULT_CHAR_WIDTH = 0.8
pub DEFAULT_CHAR_ITALIC = 0.0

// CJK fallback (slightly taller/wider than default)
pub CJK_CHAR_HEIGHT = 0.9
pub CJK_CHAR_DEPTH = 0.2
pub CJK_CHAR_WIDTH = 1.0

// Placeholder (codepoint U+2B1A → MathLive uses larger box)
pub PLACEHOLDER_CHAR_HEIGHT = 0.8
pub PLACEHOLDER_CHAR_DEPTH = 0.2
pub PLACEHOLDER_CHAR_WIDTH = 0.8

// ============================================================
// EXTRA_CHARACTER_MAP — Latin-1 + Cyrillic substitutions
// Ported from MathLive font-metrics.ts. Maps an unmapped character
// to a fallback character whose metrics we DO have.
// ============================================================

pub EXTRA_CHARACTER_MAP = {
    // NBSP and ZWSP fallbacks are handled in _default_metrics (see is_cjk).
    // Latin-1
    'Å': 'A',
    'Ç': 'C',
    'Ð': 'D',
    'Þ': 'o',
    'å': 'a',
    'ç': 'c',
    'ð': 'd',
    'þ': 'o',
    // Cyrillic uppercase
    'А': 'A', 'Б': 'B', 'В': 'B', 'Г': 'F', 'Д': 'A',
    'Е': 'E', 'Ж': 'K', 'З': '3', 'И': 'N', 'Й': 'N',
    'К': 'K', 'Л': 'N', 'М': 'M', 'Н': 'H', 'О': 'O',
    'П': 'N', 'Р': 'P', 'С': 'C', 'Т': 'T', 'У': 'y',
    'Ф': 'O', 'Х': 'X', 'Ц': 'U', 'Ч': 'h', 'Ш': 'W',
    'Щ': 'W', 'Ъ': 'B', 'Ы': 'X', 'Ь': 'B', 'Э': '3',
    'Ю': 'X', 'Я': 'R',
    // Cyrillic lowercase
    'а': 'a', 'б': 'b', 'в': 'a', 'г': 'r', 'д': 'y',
    'е': 'e', 'ж': 'm', 'з': 'e', 'и': 'n', 'й': 'n',
    'к': 'n', 'л': 'n', 'м': 'm', 'н': 'n', 'о': 'o',
    'п': 'n', 'р': 'p', 'с': 'c', 'т': 'o', 'у': 'y',
    'ф': 'b', 'х': 'x', 'ц': 'n', 'ч': 'n', 'ш': 'w',
    'щ': 'w', 'ъ': 'a', 'ы': 'm', 'ь': 'a', 'э': 'e',
    'ю': 'm', 'я': 'r'
}

// ============================================================
// CJK detection
// Hiragana [U+3040-U+309F] | Katakana [U+30A0-U+30FF]
// CJK Unified Ideographs [U+4E00-U+9FAF] | Hangul [U+AC00-U+D7AF]
// ============================================================

pub fn is_cjk(ch) {
    if (ch == null or len(ch) == 0) false
    else {
        let cp = code_point(ch, 0)
        (cp >= 12352 and cp <= 12447) or
        (cp >= 12448 and cp <= 12543) or
        (cp >= 19968 and cp <= 40879) or
        (cp >= 44032 and cp <= 55215)
    }
}

// helper: extract a Unicode code point from a single-char string. Uses
// Lambda's built-in ord() which returns the code point of the first
// character.
pub fn code_point(s, i) {
    if (s == null or len(s) == 0) 0
    else if (i == 0) ord(s)
    else if (len(s) > i) ord(s[i])
    else 0
}

// ============================================================
// getCharacterMetrics — full cascade ported from MathLive
// Returns a map: {default: bool, depth, height, italic, skew, width}
//
// font_name: "Main-Regular" / "cmr" / "main"
//            "Math-Italic" / "cmmi" / "mathit"
//            "AMS-Regular" / "ams"
// ============================================================

fn _normalize_font(font_name) {
    if (font_name == "Main-Regular" or font_name == "cmr" or font_name == "main") "cmr"
    else if (font_name == "Math-Italic" or font_name == "cmmi" or font_name == "mathit") "cmmi"
    else if (font_name == "AMS-Regular" or font_name == "ams") "ams"
    else font_name
}

fn _metrics_record(m, is_default) {
    {
        default: is_default,
        depth: m[0],
        height: m[1],
        italic: m[2],
        skew: m[3],
        width: m[4]
    }
}

pub fn get_character_metrics(ch, font_name) {
    if (ch == null) get_character_metrics("M", font_name)
    else {
        let font = _normalize_font(font_name)
        let direct = metrics_data.lookup(ch, font)
        if (direct != null) _metrics_record(direct, false)
        else {
            // Try EXTRA_CHARACTER_MAP fallback
            let fallback = EXTRA_CHARACTER_MAP[ch]
            if (fallback != null) {
                let fb = metrics_data.lookup(fallback, font)
                if (fb != null) _metrics_record(fb, true)
                else _default_metrics(ch)
            }
            else _default_metrics(ch)
        }
    }
}

fn _default_metrics(ch) {
    if (is_cjk(ch)) {
        {default: true, depth: CJK_CHAR_DEPTH, height: CJK_CHAR_HEIGHT, italic: 0.0, skew: 0.0, width: CJK_CHAR_WIDTH}
    } else {
        {default: true, depth: DEFAULT_CHAR_DEPTH, height: DEFAULT_CHAR_HEIGHT, italic: 0.0, skew: 0.0, width: DEFAULT_CHAR_WIDTH}
    }
}

// ============================================================
// Mathstyle transitions (mirrors MathLive's mathstyle.ts)
//   D    display
//   Dc   display-cramped
//   T    text
//   Tc   text-cramped
//   S    script
//   Sc   script-cramped
//   SS   scriptscript
//   SSc  scriptscript-cramped
// ============================================================

// the style your superscript lives in
pub fn sup_style(style) {
    if (style == "display" or style == "text") "script"
    else if (style == "display-cramped" or style == "text-cramped") "script-cramped"
    else if (style == "script" or style == "script-cramped") "scriptscript"
    else if (style == "scriptscript" or style == "scriptscript-cramped") "scriptscript"
    else "script"
}

// the style your subscript lives in (always cramped at the next level)
pub fn sub_style(style) {
    if (style == "display" or style == "text" or style == "display-cramped" or style == "text-cramped") "script-cramped"
    else if (style == "script" or style == "script-cramped") "scriptscript-cramped"
    else if (style == "scriptscript" or style == "scriptscript-cramped") "scriptscript-cramped"
    else "script-cramped"
}

// the style the numerator of a \frac lives in
pub fn frac_num_style(style) {
    if (style == "display") "text"
    else if (style == "display-cramped") "text-cramped"
    else if (style == "text") "script"
    else if (style == "text-cramped") "script-cramped"
    else if (style == "script") "scriptscript"
    else if (style == "script-cramped") "scriptscript-cramped"
    else "scriptscript"
}

// the style the denominator of a \frac lives in (always cramped)
pub fn frac_den_style(style) {
    if (style == "display" or style == "display-cramped") "text-cramped"
    else if (style == "text" or style == "text-cramped") "script-cramped"
    else if (style == "script" or style == "script-cramped") "scriptscript-cramped"
    else "scriptscript-cramped"
}

// the cramped version of a given style
pub fn cramp(style) {
    if (style == "display") "display-cramped"
    else if (style == "text") "text-cramped"
    else if (style == "script") "script-cramped"
    else if (style == "scriptscript") "scriptscript-cramped"
    else style
}

pub fn is_cramped(style) {
    style == "display-cramped" or style == "text-cramped" or
    style == "script-cramped" or style == "scriptscript-cramped"
}

// derive the canonical "non-cramped" form
pub fn base_style(style) {
    if (style == "display-cramped") "display"
    else if (style == "text-cramped") "text"
    else if (style == "script-cramped") "script"
    else if (style == "scriptscript-cramped") "scriptscript"
    else style
}

// MathLive's style_index already maps both display and text to slot 0.
// Adding the cramped variants here preserves that.
pub fn style_index_full(style) {
    let s = base_style(style)
    if (s == "display" or s == "text") 0
    else if (s == "script") 1
    else if (s == "scriptscript") 2
    else 0
}

pub fn style_scale_full(style) {
    let s = base_style(style)
    if (s == "display" or s == "text") 1.0
    else if (s == "script") 0.7
    else if (s == "scriptscript") 0.5
    else 1.0
}
