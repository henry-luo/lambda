// latex/elements/color.ls — Color command rendering for text mode
// \textcolor{color}{text}, \color{color}, \colorbox{color}{text},
// \fcolorbox{border}{bg}{text}, \definecolor{name}{model}{spec}, \pagecolor{color}

import util: lambda.latex.util
import xcolor: lambda.latex.packages.xcolor

// ============================================================
// Named color map (LaTeX xcolor standard named colors)
// ============================================================

let NAMED_COLORS = {
    'red':       "#d32f2f",
    'blue':      "#1976d2",
    'green':     "#388e3c",
    'black':     "#000000",
    'white':     "#ffffff",
    'gray':      "#9e9e9e",
    'grey':      "#9e9e9e",
    'yellow':    "#fbc02d",
    'orange':    "#f57c00",
    'purple':    "#7b1fa2",
    'cyan':      "#00bcd4",
    'magenta':   "#e91e63",
    'brown':     "#795548",
    'lime':      "#cddc39",
    'olive':     "#827717",
    'pink':      "#e91e63",
    'teal':      "#009688",
    'violet':    "#9c27b0",
    'darkgray':  "#616161",
    'lightgray': "#bdbdbd"
}

// ============================================================
// Color resolution
// ============================================================

// resolve a color name/spec to a CSS color string
// custom_colors: map or entry-list of user-defined colors from \definecolor (may be null)
pub fn resolve_color(raw, custom_colors) {
    let trimmed = trim(raw)
    let mix_parts = split(trimmed, "!")
    // check custom colors first (supports both map and entry-list formats)
    let custom_val = lookup_custom_color(custom_colors, trimmed)
    if (custom_val is map and custom_val.invalid == true) null
    else if (custom_val != null) custom_val
    else if (len(mix_parts) == 3) {
        let first = resolve_color(trim(mix_parts[0]), custom_colors)
        let second = resolve_color(trim(mix_parts[2]), custom_colors)
        let mixed = xcolor.mix(first, second, trim(mix_parts[1]))
        mixed
    }
    else if (len(mix_parts) == 2) {
        let first = resolve_color(trim(mix_parts[0]), custom_colors)
        let mixed = xcolor.mix(first, "#ffffff", trim(mix_parts[1]))
        mixed
    }
    // check if already a CSS color (#hex)
    else if (len(trimmed) == 7 and starts_with(trimmed, "#") and xcolor.valid_hex(trimmed, 1)) trimmed
    // check named colors
    else if (NAMED_COLORS[trimmed] != null) NAMED_COLORS[trimmed]
    // CSS keyword parsing remains with the host; only a plain name can pass through.
    else if (css_name(trimmed, 0)) trimmed
    else null
}

fn css_name(raw, i) {
    if (raw == "") false
    else if (i >= len(raw)) true
    else {
        let ch = slice(raw, i, i + 1)
        if ((ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z"))
            css_name(raw, i + 1)
        else false
    }
}

fn lookup_custom_color(custom_colors, key) {
    if (custom_colors == null) null
    else if (custom_colors is array) util.lookup(custom_colors, key)
    else custom_colors[key]
}

// parse a \definecolor{name}{model}{spec} and return {name, css_color}
// model: "rgb" (0-1 values), "RGB" (0-255), "HTML" (hex), "named", "gray"
pub fn parse_definecolor(name_str, model_str, spec_str) {
    let css = parse_color_model(trim(model_str), trim(spec_str))
    {color_name: trim(name_str), css_color: css}
}

pub fn definition(el) {
    let args = util.command_args(el)
    if (len(args) < 3) {color_name: "", css_color: null}
    else parse_definecolor(util.text_of(args[0]), util.text_of(args[1]), util.text_of(args[2]))
}

pub fn definition_value(definition) {
    if (definition.css_color == null) {invalid: true} else definition.css_color
}

fn parse_color_model(model, spec) {
    if (model == "HTML" or model == "html")
        if (len(spec) == 6 and xcolor.valid_hex(spec, 0)) "#" ++ spec else null
    else if (model == "rgb") parse_rgb_float(spec)
    else if (model == "RGB") parse_rgb_int(spec)
    else if (model == "gray") parse_gray(spec)
    else if (model == "named") resolve_color(spec, null)
    else null
}

fn channel(value, scale) {
    let parsed = float(trim(value)) ^ { null }
    if (parsed == null or parsed < 0 or parsed > scale) null
    else int(parsed * 255.0 / scale + 0.5)
}

fn rgb_channels(spec, scale) {
    let parts = split(spec, ",")
    if (len(parts) != 3) null
    else {
        let channels = [for (part in parts) channel(part, scale)]
        if (any([for (value in channels) value == null])) null else channels
    }
}

fn rgb_css(channels) {
    if (channels == null) null
    else "rgb(" ++ channels[0] ++ "," ++ channels[1] ++ "," ++ channels[2] ++ ")"
}

// "0.2,0.4,0.6" → "rgb(51,102,153)"
fn parse_rgb_float(spec) {
    rgb_css(rgb_channels(spec, 1.0))
}

// "255,128,0" → "rgb(255,128,0)"
fn parse_rgb_int(spec) {
    rgb_css(rgb_channels(spec, 255.0))
}

// "0.5" → "rgb(128,128,128)"
fn parse_gray(spec) {
    let val = channel(spec, 1.0)
    if (val == null) null else "rgb(" ++ val ++ "," ++ val ++ "," ++ val ++ ")"
}

// ============================================================
// Render functions — called from render.ls dispatcher
// ============================================================

// \textcolor{color}{text} → <span style="color:...">text</span>
pub fn render_textcolor(el, items, custom_colors) {
    let css_color = resolve_command_color(el, 0, custom_colors);
    if (css_color == null) invalid_color(el)
    else <span class: "latex-textcolor", style: "color:" ++ css_color, for c in items { c }>
}

// \colorbox{color}{text} → <span style="background-color:...;padding:...">text</span>
pub fn render_colorbox(el, items, custom_colors) {
    let css_color = resolve_command_color(el, 0, custom_colors);
    if (css_color == null) invalid_color(el)
    else <span class: "latex-colorbox", style: "background-color:" ++ css_color ++ ";padding:0.1em 0.2em", for c in items { c }>
}

// \fcolorbox{border}{bg}{text} → <span style="border:...;background-color:...">text</span>
pub fn render_fcolorbox(el, items, custom_colors) {
    let border_color = resolve_command_color(el, 0, custom_colors)
    let bg_color = resolve_command_color(el, 1, custom_colors);
    if (border_color == null or bg_color == null) invalid_color(el)
    else <span class: "latex-fcolorbox", style: "border:1px solid " ++ border_color ++ ";background-color:" ++ bg_color ++ ";padding:0.1em 0.2em", for c in items { c }>
}

// \pagecolor{color} → null (applied via info.page_color in wrapper)
pub fn render_pagecolor(el, custom_colors) {
    // returns null — page color is handled at document level
    null
}

// \color{blue} — scoped declaration, returns CSS color string for wrapping
pub fn color_decl_style(el, custom_colors) {
    let css_color = resolve_command_color(el, 0, custom_colors)
    if (css_color == null) null else "color:" ++ css_color
}

// check if an element is a \color declaration (for find_leading_decl)
pub fn is_color_decl(tag_str) {
    tag_str == "color"
}

// wrap rendered items in a span with color style
pub fn wrap_color_decl(el, items, custom_colors) {
    let style = color_decl_style(el, custom_colors);
    if (style == null) invalid_color(el)
    else <span class: "latex-color", style: style, for c in items { c }>
}

pub fn page_color(el, custom_colors) {
    resolve_command_color(el, 0, custom_colors)
}

pub fn invalid_color(el) {
    let offset = el.source_offset
    util.unsupported_element("xcolor", "Invalid xcolor value in " ++ string(name(el)), offset)
}

// ============================================================
// Helpers
// ============================================================

// get text of a child node (handles both string and element children)
fn child_text(child) {
    trim(util.text_of(child))
}

pub fn content_start(el, color_args) {
    color_args + (if (util.optional_raw(el) != null) 1 else 0)
}

fn resolve_command_color(el, color_index, custom_colors) {
    let model = util.optional_raw(el)
    let spec = get_child_text(el, color_index + (if (model != null) 1 else 0))
    if (model != null) parse_color_model(trim(model), spec)
    else resolve_color(spec, custom_colors)
}

// get text of child at index
fn get_child_text(el, idx) {
    let args = util.command_args(el)
    if (idx < len(args)) { child_text(args[idx]) }
    else { "" }
}
