// Shared validation for the native TikZ subset. Unknown keys fail visibly.
import latex_color: lambda.latex.elements.color
import latex_util: lambda.latex.util

let COLOR_NAMES = ["black", "blue", "red", "green", "darkgreen", "orange",
    "purple", "gray", "white", "yellow", "lightgray", "Silver", "Goldenrod",
    "Blue", "Red", "RoyalBlue", "VioletRed", "LightGrey"]

pub fn normalize_key(key) {
    let spaces = replace(replace(replace(key, "\n", " "), "\t", " "),
        "\r", " ")
    latex_util.str_join([for (part in split(spaces, " ") where part != "")
        part], " ")
}

fn known_color(key, custom_colors) =>
    len([for (name in COLOR_NAMES where name == key) name]) > 0 or
    (custom_colors != null and
        latex_util.lookup(custom_colors, key) != null)

fn color_key(key, custom_colors = null) {
    let parts = split(key, "!")
    if (starts_with(key, "#") and len(key) == 7)
        latex_color.resolve_color(key, custom_colors) != null
    else if (len(parts) == 1)
        known_color(key, custom_colors)
    else if (len(parts) == 2 or len(parts) == 3) {
        let first = known_color(trim(parts[0]), custom_colors)
        let second = if (len(parts) == 2) true
            else known_color(trim(parts[2]), custom_colors)
        let amount = float(trim(parts[1])) ^ { null }
        first and second and amount != null and amount >= 0.0 and amount <= 100.0
    } else false
}

pub fn value(node, key, fallback = null) {
    let matches = [for (child in node
        where child is element and string(name(child)) == "option" and
              normalize_key(child.key) == key) child.value]
    if (len(matches) == 0) fallback else matches[len(matches) - 1]
}

pub fn has(node, key) {
    len([for (child in node
        where child is element and string(name(child)) == "option" and
              normalize_key(child.key) == key) child]) > 0
}

pub fn check(node, keys, custom_colors = null) bool^ {
    let unknown = [for (child in node
        where child is element and string(name(child)) == "option" and
              not allowed(normalize_key(child.key), keys, custom_colors)^) child.key]
    if (len(unknown) > 0)
        raise error("unsupported TikZ/PGFPlots option: " ++ unknown[0])
    else true
}

// Key handlers the TikZ parser expands; other PGF handlers (.code, .cd, .store in,
// ...) program PGF internals and are diagnosed.
pub let STYLE_HANDLERS = ["/.style", "/.style 2 args", "/.append style", "/.default", "/.pic"]

// Handler keys on a node, after rejecting handlers outside STYLE_HANDLERS.
pub fn handler_keys(node) any^ {
    let handlers = [for (child in node where child is element and
        string(name(child)) == "option" and contains(child.key, "/.")) child.key]
    let unsupported = [for (key in handlers where not any([for (suffix in STYLE_HANDLERS)
        ends_with(key, suffix)])) key]
    if (len(unsupported) > 0) raise error("unsupported PGF key handler: " ++ unsupported[0])
    else handlers
}

// A picture-level \tikzset may only define handlers: a plain key such as `>=Stealth`
// would change defaults the profile does not track, so it is diagnosed, not dropped.
pub fn check_tikzset(setting) any^ {
    let plain = [for (child in setting where child is element and
        string(name(child)) == "option" and not contains(child.key, "/.")) child.key]
    if (len(plain) > 0) raise error("unsupported TikZ \\tikzset key: " ++ plain[0])
    else handler_keys(setting)^
}

fn allowed(key, keys, custom_colors) bool^ {
    len([for (candidate in keys where candidate == key) candidate]) > 0 or
        color_key(key, custom_colors) == true
}

// Stroke width in CSS px from TikZ line-width keys; `fallback` is the inherited width.
pub fn stroke_width(node, fallback) float^ {
    let explicit = value(node, "line width", null)
    if (explicit != null) dimension_px(explicit)^
    else if (has(node, "ultra thick")) 2.2
    else if (has(node, "very thick")) 1.7
    else if (has(node, "thick")) 1.1
    else if (has(node, "thin")) 0.53
    else fallback
}

// SVG dash array from `dashed`, `dotted` or `dash pattern=on a off b`.
pub fn dash_array(node) any^ {
    let custom = value(node, "dash pattern", null)
    if (custom != null) {
        let words = [for (part in split(trim(custom), " ") where trim(part) != "")
            trim(part)]
        if (len(words) != 4 or words[0] != "on" or words[2] != "off")
            raise error("unsupported TikZ dash pattern: " ++ custom)
        else string(dimension_px(words[1])^) ++ " " ++ string(dimension_px(words[3])^)
    } else if (has(node, "dashed")) "4 4"
    else if (has(node, "dotted")) "1 2"
    else null
}

pub fn numeric_value(source) float^ {
    let raw = trim(source)
    // TeX accepts a leading decimal point and unary plus; JSON numbers do not.
    let unsigned = if (starts_with(raw, "+")) slice(raw, 1, len(raw)) else raw
    let normalized = if (starts_with(unsigned, "-."))
        "-0" ++ slice(unsigned, 1, len(unsigned))
        else if (starts_with(unsigned, ".")) "0" ++ unsigned
        else unsigned
    let parsed = parse(normalized, "json") ^ {
        raise error("expected finite numeric TikZ option: " ++ source)
    }
    if (parsed is int or parsed is float) float(parsed)
    else raise error("expected numeric TikZ option: " ++ source)
}

// CSS pixel conversion is explicit because TeX pt and CSS px differ.
pub fn dimension_px(source, text_width_px = null) float^ {
    let s = trim(source)
    let units = if (ends_with(s, "\\textwidth")) "textwidth"
        else if (ends_with(s, "cm")) "cm"
        else if (ends_with(s, "mm")) "mm"
        else if (ends_with(s, "pt")) "pt"
        else if (ends_with(s, "bp")) "bp"
        else if (ends_with(s, "in")) "in"
        else raise error("unsupported TikZ dimension: " ++ source)
    let unit_length = if (units == "textwidth") len("\\textwidth") else len(units)
    let magnitude = numeric_value(slice(s, 0, len(s) - unit_length))^
    if (magnitude <= 0.0) raise error("TikZ dimension must be positive")
    else if (units == "textwidth") {
        if (text_width_px == null) raise error("\\textwidth needs a document viewport")
        else magnitude * float(text_width_px)
    }
    else if (units == "cm") magnitude * 96.0 / 2.54
    else if (units == "mm") magnitude * 96.0 / 25.4
    else if (units == "pt") magnitude * 96.0 / 72.27
    else if (units == "bp") magnitude * 96.0 / 72.0
    else magnitude * 96.0
}

pub fn color(node, fallback, custom_colors = null) string^ {
    let named = [for (child in node where child is element and
        string(name(child)) == "option" and child.value == "" and
        color_key(child.key, custom_colors)) child.key]
    let color_name = if (len(named) > 0) named[len(named) - 1]
        else value(node, "color", fallback)
    color_value(color_name, custom_colors)^
}

pub fn color_value(color_name, custom_colors = null) string^ {
    if (not color_key(color_name, custom_colors))
        raise error("unsupported TikZ color: " ++ color_name)
    else {
        let custom = if (custom_colors == null) null else latex_util.lookup(custom_colors, color_name)
        let resolved = if (custom == null and not contains(color_name, "!")) color_name
            else latex_color.resolve_color(color_name, custom_colors)
        if (resolved == null) raise error("invalid TikZ color: " ++ color_name)
        else resolved
    }
}
