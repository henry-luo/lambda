// math/metrics.ls — TeX font metric parameters
// Values from MathLive font-metrics.ts (sourced from TeX's cmsy10/cmsy7/cmsy5)
// Each value is an array [textstyle, scriptstyle, scriptscriptstyle]

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
