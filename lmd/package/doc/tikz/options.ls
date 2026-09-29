// Shared validation for the native TikZ subset. Unknown keys fail visibly.

pub fn value(node, key, fallback = null) {
    let matches = [for (child in node
        where child is element and string(name(child)) == "option" and
              child.key == key) child.value]
    if (len(matches) == 0) fallback else matches[len(matches) - 1]
}

pub fn has(node, key) {
    len([for (child in node
        where child is element and string(name(child)) == "option" and
              child.key == key) child]) > 0
}

pub fn check(node, keys) bool^ {
    let unknown = [for (child in node
        where child is element and string(name(child)) == "option" and
              not allowed(child.key, keys)) child.key]
    if (len(unknown) > 0)
        raise error("unsupported TikZ/PGFPlots option: " ++ unknown[0])
    else true
}

fn allowed(key, keys) {
    len([for (candidate in keys where candidate == key) candidate]) > 0
}

pub fn numeric_value(source) float^ {
    let parsed = parse(trim(source), "json") ^ {
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

pub fn color(node, fallback) string^ {
    let named = [for (candidate in ["black", "blue", "red", "green", "darkgreen",
            "orange", "purple", "gray"]
        where has(node, candidate)) candidate]
    let color_name = if (len(named) > 0) named[0] else value(node, "color", fallback)
    color_value(color_name)^
}

pub fn color_value(color_name) string^ {
    if (allowed(color_name, ["black", "blue", "red", "green", "darkgreen",
            "orange", "purple", "gray"])) color_name
    else raise error("unsupported TikZ color: " ++ color_name)
}
