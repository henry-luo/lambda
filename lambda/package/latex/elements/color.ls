// latex/elements/color.ls — Color command rendering for text mode
// \textcolor{color}{text}, \color{color}, \colorbox{color}{text},
// \fcolorbox{border}{bg}{text}, \definecolor{name}{model}{spec}, \pagecolor{color}

// ============================================================
// Named color map (LaTeX xcolor standard named colors)
// ============================================================

let NAMED_COLORS = {
    "red":       "#d32f2f",
    "blue":      "#1976d2",
    "green":     "#388e3c",
    "black":     "#000000",
    "white":     "#ffffff",
    "gray":      "#9e9e9e",
    "grey":      "#9e9e9e",
    "yellow":    "#fbc02d",
    "orange":    "#f57c00",
    "purple":    "#7b1fa2",
    "cyan":      "#00bcd4",
    "magenta":   "#e91e63",
    "brown":     "#795548",
    "lime":      "#cddc39",
    "olive":     "#827717",
    "pink":      "#e91e63",
    "teal":      "#009688",
    "violet":    "#9c27b0",
    "darkgray":  "#616161",
    "lightgray": "#bdbdbd"
}

// ============================================================
// Color resolution
// ============================================================

// resolve a color name/spec to a CSS color string
// custom_colors: map of user-defined colors from \definecolor (may be null)
pub fn resolve_color(raw, custom_colors) {
    let trimmed = trim(raw)
    // check custom colors first
    if (custom_colors != null and custom_colors[trimmed] != null) custom_colors[trimmed]
    // check if already a CSS color (#hex)
    else if (len(trimmed) > 0 and slice(trimmed, 0, 1) == "#") trimmed
    // check named colors
    else if (NAMED_COLORS[trimmed] != null) NAMED_COLORS[trimmed]
    // pass through as-is (browser may know the name)
    else trimmed
}

// parse a \definecolor{name}{model}{spec} and return {name, css_color}
// model: "rgb" (0-1 values), "RGB" (0-255), "HTML" (hex), "named", "gray"
pub fn parse_definecolor(name_str, model_str, spec_str) {
    let css = parse_color_model(trim(model_str), trim(spec_str))
    {color_name: trim(name_str), css_color: css}
}

fn parse_color_model(model, spec) {
    if (model == "HTML" or model == "html") "#" ++ spec
    else if (model == "rgb") parse_rgb_float(spec)
    else if (model == "RGB") parse_rgb_int(spec)
    else if (model == "gray") parse_gray(spec)
    else if (model == "named") resolve_color(spec, null)
    else spec
}

// "0.2,0.4,0.6" → "rgb(51,102,153)"
fn parse_rgb_float(spec) {
    let parts = split(spec, ",")
    if (len(parts) >= 3) {
        let r = int(float(trim(parts[0])) * 255.0)
        let g = int(float(trim(parts[1])) * 255.0)
        let b = int(float(trim(parts[2])) * 255.0)
        "rgb(" ++ string(r) ++ "," ++ string(g) ++ "," ++ string(b) ++ ")"
    }
    else { spec }
}

// "255,128,0" → "rgb(255,128,0)"
fn parse_rgb_int(spec) {
    let parts = split(spec, ",")
    if (len(parts) >= 3) {
        let r = trim(parts[0])
        let g = trim(parts[1])
        let b = trim(parts[2])
        "rgb(" ++ r ++ "," ++ g ++ "," ++ b ++ ")"
    }
    else { spec }
}

// "0.5" → "rgb(128,128,128)"
fn parse_gray(spec) {
    let val = int(float(spec) * 255.0)
    "rgb(" ++ string(val) ++ "," ++ string(val) ++ "," ++ string(val) ++ ")"
}

// ============================================================
// Render functions — called from render2.ls dispatcher
// ============================================================

// \textcolor{color}{text} → <span style="color:...">text</span>
pub fn render_textcolor(el, items, custom_colors) {
    let color_raw = get_first_text(el)
    let css_color = resolve_color(color_raw, custom_colors)
    <span class: "latex-textcolor", style: "color:" ++ css_color; for c in items { c }>
}

// \colorbox{color}{text} → <span style="background-color:...;padding:...">text</span>
pub fn render_colorbox(el, items, custom_colors) {
    let color_raw = get_first_text(el)
    let css_color = resolve_color(color_raw, custom_colors)
    <span class: "latex-colorbox", style: "background-color:" ++ css_color ++ ";padding:0.1em 0.2em"; for c in items { c }>
}

// \fcolorbox{border}{bg}{text} → <span style="border:...;background-color:...">text</span>
pub fn render_fcolorbox(el, items, custom_colors) {
    let border_raw = get_child_text(el, 0)
    let bg_raw = get_child_text(el, 1)
    let border_color = resolve_color(border_raw, custom_colors)
    let bg_color = resolve_color(bg_raw, custom_colors)
    <span class: "latex-fcolorbox", style: "border:1px solid " ++ border_color ++ ";background-color:" ++ bg_color ++ ";padding:0.1em 0.2em"; for c in items { c }>
}

// \pagecolor{color} → null (applied via info.page_color in wrapper)
pub fn render_pagecolor(el, custom_colors) {
    // returns null — page color is handled at document level
    null
}

// \color{blue} — scoped declaration, returns CSS color string for wrapping
pub fn color_decl_style(el, custom_colors) {
    let color_raw = get_first_text(el)
    let css_color = resolve_color(color_raw, custom_colors)
    "color:" ++ css_color
}

// check if an element is a \color declaration (for find_leading_decl)
pub fn is_color_decl(tag_str) {
    tag_str == "color"
}

// wrap rendered items in a span with color style
pub fn wrap_color_decl(el, items, custom_colors) {
    let style = color_decl_style(el, custom_colors)
    <span class: "latex-color", style: style; for c in items { c }>
}

// ============================================================
// Helpers
// ============================================================

// get text of a child node (handles both string and element children)
fn child_text(child) {
    if (child is element and len(child) > 0) { trim(string(child[0])) }
    else { trim(string(child)) }
}

// get text of first child (the color argument)
fn get_first_text(el) {
    if (len(el) > 0) { child_text(el[0]) }
    else { "" }
}

// get text of child at index
fn get_child_text(el, idx) {
    if (idx < len(el)) { child_text(el[idx]) }
    else { "" }
}
