// Shared validation for the native TikZ subset. Unknown keys fail visibly.
import latex_color: lambda.latex.elements.color
import latex_util: lambda.latex.util

let COLOR_NAMES = ["black", "blue", "red", "green", "darkgreen", "orange",
    "purple", "gray", "white", "yellow", "lightgray", "Silver", "Goldenrod",
    "Blue", "Red", "RoyalBlue", "VioletRed", "LightGrey"]

fn normalize_key(key) {
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

fn allowed(key, keys, custom_colors) bool^ {
    len([for (candidate in keys where candidate == key) candidate]) > 0 or
        color_key(key, custom_colors) == true or key == "-{Stealth}" or
        (starts_with(key, "-{Stealth[") and ends_with(key, "]}")^)
}

pub fn stealth_length(node) float^ {
    let keys = [for (child in node where child is element and
        string(name(child)) == "option" and
        (child.key == "-{Stealth}" or
         (starts_with(child.key, "-{Stealth[") and
          ends_with(child.key, "]}")))) child.key]
    if (len(keys) == 0) 0.0
    else if (len(keys) != 1) raise error("duplicate Stealth arrow tip")
    else if (keys[0] == "-{Stealth}") 8.0
    else {
        let inner = slice(keys[0], len("-{Stealth["), len(keys[0]) - 2)
        if (not starts_with(inner, "length="))
            raise error("unsupported Stealth arrow tip option")
        else dimension_px(trim(slice(inner, len("length="), len(inner))))^
    }
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
