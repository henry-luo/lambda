// math/metrics_data.ls — Font metrics accessors.
// The generated metrics are parsed from metrics_data.mark so module loading
// does not compile the static data as Lambda source.

let metric_tables = input(sys.lambda.home# ++ "/package/math/metrics_data.mark", 'mark')^

// Lookup: single-character string + font name → metrics or null
pub fn lookup(ch, font_name) {
    if (ch == null) null
    else if (font_name == "cmr" or font_name == "main") metric_tables.main_regular[ch]
    else if (font_name == "mathit" or font_name == "cmmi") metric_tables.math_italic[ch]
    else if (font_name == "ams") metric_tables.ams_regular[ch]
    else if (font_name == "mathbf" or font_name == "bold") metric_tables.main_bold[ch]
    else if (font_name == "tt") metric_tables.typewriter[ch]
    else if (font_name == "frak") metric_tables.fraktur[ch]
    else if (font_name == "script") metric_tables.script_font[ch]
    else if (font_name == "cal") metric_tables.caligraphic[ch]
    else if (font_name == "sans") metric_tables.sans_serif[ch]
    else null
}

// Convenience accessors
pub fn depth_of(metrics)  { if (metrics == null) null else metrics[0] }
pub fn height_of(metrics) { if (metrics == null) null else metrics[1] }
pub fn italic_of(metrics) { if (metrics == null) null else metrics[2] }
pub fn skew_of(metrics)   { if (metrics == null) null else metrics[3] }
pub fn width_of(metrics)  { if (metrics == null) null else metrics[4] }
// Full-precision (5dp) values — for strut emission and accent centering.
pub fn height_exact_of(metrics) { if (metrics == null) null else metrics[5] }
pub fn depth_exact_of(metrics)  { if (metrics == null) null else metrics[6] }
pub fn width_raw_of(metrics)    { if (metrics == null) null else metrics[7] }
