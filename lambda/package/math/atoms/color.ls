// math/atoms/color.ls — Color command rendering (\textcolor, \color, \colorbox)
// Handles foreground and background color wrapping.

import box: lambda.package.math.box
import ctx: lambda.package.math.context
import css: lambda.package.math.css

// ============================================================
// Color command rendering
// ============================================================

// render a color_command AST node
// node has: cmd, color, content attributes
// render_fn: top-level render function for recursive calls
pub fn render(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let color = if (cmd == "\\colorbox") resolve_background_color(node) else resolve_color(node)
    let new_ctx = ctx.derive(context, {color: color})

    if (cmd == "\\colorbox") render_colorbox(node, new_ctx, color, render_fn)
    else render_foreground(node, new_ctx, color, render_fn)
}

// ============================================================
// Foreground color (\textcolor, \color)
// ============================================================

fn render_foreground(node, context, color, render_fn) {
    if (node.content != null) {
        let content_box = render_fn(node.content, context)
        box.with_color(content_box, color)
    } else {
        box.text_box("", null, "ord")
    }
}

// ============================================================
// Background color (\colorbox)
// ============================================================

fn render_colorbox(node, context, bg_color, render_fn) {
    let content_ctx = ctx.derive(context, {colorbox_content: true})
    let content_box = if (node.content != null) render_fn(node.content, content_ctx)
        else box.text_box("", null, "ord")
    with_background(content_box, bg_color)
}

pub fn with_background(content_box, bg_color) {
    let style = "background-color:" ++ bg_color ++ ";--bg-color:" ++ bg_color ++
        ";display:inline-block;position:relative"
    let children = box.elements_of(content_box)
    {
        element: <span class: css.BG, style: style;
            for (child in children) child
        >,
        height: content_box.height,
        depth: content_box.depth,
        width: content_box.width,
        type: "ord",
        italic: 0.0,
        skew: 0.0,
        max_font_size: if (content_box.max_font_size != null)
            content_box.max_font_size else content_box.height,
        model: "ml",
        suppress_hbox_text_depth: content_box.suppress_hbox_text_depth,
        is_colorbox: true
    }
}

// ============================================================
// Color resolution
// ============================================================

let basic_colors = {
    red: "#d7170b", blue: "#0d80f2", green: "#21ba3a",
    black: "#000000", white: "#ffffff", gray: "#a6a6a6", grey: "#a6a6a6",
    yellow: "#ffc02b", orange: "#fe8a2b", purple: "#a219e6",
    cyan: "#13a7ec", magenta: "#eb4799", brown: "#792500",
    lime: "#63b215", olive: "#3c8031", olivegreen: "#3c8031",
    pink: "#f282b4", teal: "#00b4ce", tealblue: "#17cfcf",
    violet: "#6633cc", darkgray: "#888888", darkgrey: "#888888",
    lightgray: "#d3d3d3", lightgrey: "#d3d3d3",
    m1: "#993d71", m2: "#998b3d", m3: "#3d9956",
    m4: "#3d5a99", m5: "#993d90", m6: "#996d3d",
    m7: "#43993d", m8: "#3d7999", m9: "#843d99"
}

let xcolor_colors = {
    Apricot: "#fbb982", Aquamarine: "#00b5be", Bittersweet: "#c04f17",
    BlueGreen: "#00b3b8", BlueViolet: "#473992", BrickRed: "#b6321c",
    BurntOrange: "#f7921d", CadetBlue: "#74729a", CarnationPink: "#f282b4",
    Cerulean: "#00a2e3", CornflowerBlue: "#41b0e4", Dandelion: "#fdbc42",
    DarkOrchid: "#a4538a", Emerald: "#00a99d", ForestGreen: "#009b55",
    Fuchsia: "#8c368c", Goldenrod: "#ffdf42", GreenYellow: "#dfe674",
    JungleGreen: "#00a99a", Lavender: "#f49ec4", LimeGreen: "#63b215",
    Mahogany: "#a9341f", Maroon: "#af3235", Melon: "#f89e7b",
    MidnightBlue: "#006795", Mulberry: "#a93c93", NavyBlue: "#006eb8",
    OrangeRed: "#ed135a", Orchid: "#af72b0", Peach: "#f7965a",
    Periwinkle: "#7977b8", PineGreen: "#008b72", Plum: "#92268f",
    ProcessBlue: "#00b0f0", RawSienna: "#974006", RedOrange: "#f26035",
    RedViolet: "#a1246b", Rhodamine: "#ef559f", RoyalBlue: "#0071bc",
    RoyalPurple: "#613f99", RubineRed: "#ed017d", Salmon: "#f69289",
    SeaGreen: "#3fbc9d", Sepia: "#671800", SkyBlue: "#46c5dd",
    SpringGreen: "#c6dc67", Tan: "#da9d76", Thistle: "#d883b7",
    Turquoise: "#00b4ce", VioletRed: "#ef58a0", WildStrawberry: "#ee2967",
    YellowGreen: "#98cc70", YellowOrange: "#faa21a"
}

// extract and resolve color from node
fn resolve_color(node) {
    let raw = if (node.color != null) get_color_text(node.color) else "black"
    resolve_named_color(raw)
}

fn resolve_background_color(node) {
    let raw = if (node.color != null) get_color_text(node.color) else "black"
    resolve_background_raw(raw)
}

// get text from color node (may be element or string)
fn get_color_text(color_node) {
    if (color_node is string) string(color_node)
    else if (color_node is symbol) string(color_node)
    else if (color_node is element) get_element_text(color_node)
    else string(color_node)
}

fn get_element_text(el) {
    let n = len(el)
    if (n == 0) element_text_value(el)
    else concat_children(el, 0, n, "")
}

fn element_text_value(el) {
    if (el.value != null) string(el.value)
    else if (el.name != null) string(el.name)
    else if (el.cmd != null) string(el.cmd)
    else ""
}

fn concat_children(el, i, n, acc) {
    if (i >= n) acc
    else
        (let child = el[i],
         let txt = if (child is string) string(child)
            else if (child is symbol) string(child)
            else if (child is element) get_element_text(child)
            else "",
         concat_children(el, i + 1, n, acc ++ txt))
}

// map named LaTeX colors to CSS colors
fn resolve_named_color(raw) {
    let compact = remove_spaces(raw, 0, "")
    let lower_raw = lower_ascii(raw)
    let lower = lower_ascii(compact)
    if (len(compact) > 0 and slice(compact, 0, 1) == "#") normalize_hex(compact)
    else if (contains(compact, "!")) normalize_named_mix(compact)
    else if (starts_with(lower_raw, "rgb")) normalize_rgb_text(raw)
    else resolve_by_name(compact, lower)
}

pub fn resolve_raw(raw) {
    resolve_named_color(raw)
}

pub fn resolve_background_raw(raw) {
    let compact = remove_spaces(raw, 0, "")
    let lower = lower_ascii(compact)
    if (lower == "red") "#fbbbb6"
    else if (lower == "yellow") "#fff1c2"
    else resolve_named_color(raw)
}

fn resolve_by_name(name, lower) {
    xcolor_colors[name] or basic_colors[lower] or name
}

fn remove_spaces(text, i, acc) {
    if (i >= len(text)) acc
    else
        (let ch = slice(text, i, i + 1),
         let next = if (ch == " " or ch == "\n" or ch == "\t" or ch == "\r") acc else acc ++ ch,
         remove_spaces(text, i + 1, next))
}

fn lower_ascii(text) {
    lower_ascii_at(text, 0, "")
}

fn lower_ascii_at(text, i, acc) {
    if (i >= len(text)) acc
    else lower_ascii_at(text, i + 1, acc ++ lower_char(slice(text, i, i + 1)))
}

fn lower_char(ch) {
    if (ch == "A") "a" else if (ch == "B") "b" else if (ch == "C") "c"
    else if (ch == "D") "d" else if (ch == "E") "e" else if (ch == "F") "f"
    else if (ch == "G") "g" else if (ch == "H") "h" else if (ch == "I") "i"
    else if (ch == "J") "j" else if (ch == "K") "k" else if (ch == "L") "l"
    else if (ch == "M") "m" else if (ch == "N") "n" else if (ch == "O") "o"
    else if (ch == "P") "p" else if (ch == "Q") "q" else if (ch == "R") "r"
    else if (ch == "S") "s" else if (ch == "T") "t" else if (ch == "U") "u"
    else if (ch == "V") "v" else if (ch == "W") "w" else if (ch == "X") "x"
    else if (ch == "Y") "y" else if (ch == "Z") "z" else ch
}

fn starts_with(text, prefix) {
    (len(text) >= len(prefix)) and slice(text, 0, len(prefix)) == prefix
}

fn ends_with(text, suffix) {
    (len(text) >= len(suffix)) and slice(text, len(text) - len(suffix), len(text)) == suffix
}

fn normalize_hex(raw) {
    if (contains(raw, "!")) normalize_hex_mix(raw)
    else
        (let lower = lower_ascii(raw),
         let h = slice(lower, 1, len(lower)),
         if (len(h) == 3)
            "#" ++ slice(h, 0, 1) ++ slice(h, 0, 1) ++
            slice(h, 1, 2) ++ slice(h, 1, 2) ++
            slice(h, 2, 3) ++ slice(h, 2, 3)
         else lower)
}

fn normalize_rgb_text(raw) {
    let numbers = collect_rgb_numbers(raw, 0, "", [])
    let well_formed = starts_with(lower_ascii(raw), "rgb(") and ends_with(raw, ")") and len(numbers) == 3 and
        not contains(raw, ".") and not contains(raw, "-")
    let split = if (well_formed) {r: int(numbers[0]), g: int(numbers[1]), b: int(numbers[2])}
        else split_rgb_digits(collect_digits(raw, 0, ""))
    if (well_formed and split != null) "#" ++ hex_byte(split.r) ++ hex_byte(split.g) ++ hex_byte(split.b)
    else format_rgb_raw(raw)
}

fn normalize_hex_mix(raw) =>
    (let parts = split_on_bang(raw, 0, "", []),
     if ((len(parts) == 2) or (len(parts) == 3))
        (let base = hex_color_to_rgb(parts[0]),
         let pct = int(parts[1]),
         let target_raw = if (len(parts) == 3) parts[2] else "#ffffff",
         let target = hex_color_to_rgb(target_raw),
         if (base != null and target != null)
            "#" ++ hex_byte(mix_channel(base.r, target.r, pct)) ++
            hex_byte(mix_channel(base.g, target.g, pct)) ++
            hex_byte(mix_channel(base.b, target.b, pct))
         else raw)
     else raw)

fn normalize_named_mix(raw) =>
    (let parts = split_on_bang(raw, 0, "", []),
     if ((len(parts) == 2) or (len(parts) == 3))
        (let base = color_name_to_rgb(parts[0]),
         let pct = int(parts[1]),
         let target_raw = if (len(parts) == 3) parts[2] else "white",
         let target = color_name_to_rgb(target_raw),
         if (base != null and target != null)
            "#" ++ hex_byte(mix_channel(base.r, target.r, pct)) ++
            hex_byte(mix_channel(base.g, target.g, pct)) ++
            hex_byte(mix_channel(base.b, target.b, pct))
         else raw)
     else raw)

fn color_name_to_rgb(raw) {
    let name = strip_leading_minus(raw)
    let lower = lower_ascii(name)
    let resolved = resolve_by_name(name, lower)
    if (len(resolved) > 0 and slice(resolved, 0, 1) == "#")
        hex_color_to_rgb(resolved)
    else null
}

fn strip_leading_minus(raw) {
    if (len(raw) > 0 and slice(raw, 0, 1) == "-") slice(raw, 1, len(raw))
    else raw
}

fn split_on_bang(text, i, current, parts) {
    if (i >= len(text)) parts ++ [current]
    else
        (let ch = slice(text, i, i + 1),
         if (ch == "!")
            split_on_bang(text, i + 1, "", parts ++ [current])
         else split_on_bang(text, i + 1, current ++ ch, parts))
}

fn mix_channel(base, target, pct) {
    int(round((float(base) * float(pct) + float(target) * float(100 - pct)) / 100.0))
}

fn hex_color_to_rgb(raw) =>
    if ((len(raw) > 0) and (slice(raw, 0, 1) == "#"))
        (let h = slice(normalize_hex_no_mix(raw), 1, len(normalize_hex_no_mix(raw))),
         if (len(h) == 6)
            {r: hex_pair_value(slice(h, 0, 2)),
             g: hex_pair_value(slice(h, 2, 4)),
             b: hex_pair_value(slice(h, 4, 6))}
         else null)
    else null

fn normalize_hex_no_mix(raw) {
    let lower = lower_ascii(raw)
    let h = slice(lower, 1, len(lower))
    if (len(h) == 3)
        "#" ++ slice(h, 0, 1) ++ slice(h, 0, 1) ++
        slice(h, 1, 2) ++ slice(h, 1, 2) ++
        slice(h, 2, 3) ++ slice(h, 2, 3)
    else lower
}

fn hex_pair_value(pair) {
    hex_value(slice(pair, 0, 1)) * 16 + hex_value(slice(pair, 1, 2))
}

fn hex_value(ch) {
    if (ch == "0") 0 else if (ch == "1") 1 else if (ch == "2") 2 else if (ch == "3") 3
    else if (ch == "4") 4 else if (ch == "5") 5 else if (ch == "6") 6 else if (ch == "7") 7
    else if (ch == "8") 8 else if (ch == "9") 9 else if (ch == "a") 10 else if (ch == "b") 11
    else if (ch == "c") 12 else if (ch == "d") 13 else if (ch == "e") 14 else 15
}

fn format_rgb_raw(raw) {
    format_rgb_raw_at(raw, 0, "")
}

fn format_rgb_raw_at(raw, i, acc) {
    if (i >= len(raw)) acc
    else
        (let ch = slice(raw, i, i + 1),
         let next = if (ch == ",") acc ++ ", " else acc ++ ch,
         let step = if (ch == "," and i + 1 < len(raw) and slice(raw, i + 1, i + 2) == " ") 2 else 1,
         format_rgb_raw_at(raw, i + step, next))
}

fn collect_rgb_numbers(text, i, current, nums) {
    if (i >= len(text))
        if (current == "") nums else nums ++ [current]
    else
        (let ch = slice(text, i, i + 1),
         if (is_digit(ch))
            collect_rgb_numbers(text, i + 1, current ++ ch, nums)
         else
            (let next_nums = if (current == "") nums else nums ++ [current],
             collect_rgb_numbers(text, i + 1, "", next_nums)))
}

fn collect_digits(text, i, acc) {
    if (i >= len(text)) acc
    else
        (let ch = slice(text, i, i + 1),
         let next = if (is_digit(ch)) acc ++ ch else acc,
         collect_digits(text, i + 1, next))
}

fn is_digit(ch) {
    ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
    ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9"
}

fn split_rgb_digits(digits) {
    split_rgb_first(digits, 3)
}

fn split_rgb_first(digits, r_len) {
    if (r_len < 1 or r_len >= len(digits)) null
    else
        (let r_txt = slice(digits, 0, r_len),
         let split = split_rgb_second(digits, r_txt, r_len, 3),
         if (split != null) split else split_rgb_first(digits, r_len - 1))
}

fn split_rgb_second(digits, r_txt, start, g_len) {
    let rem = len(digits) - start - g_len
    if (g_len < 1 or rem < 1) null
    else if (rem <= 3)
        (let g_txt = slice(digits, start, start + g_len),
         let b_txt = slice(digits, start + g_len, len(digits)),
         if (valid_byte_text(r_txt) and valid_byte_text(g_txt) and valid_byte_text(b_txt))
            {r: int(r_txt), g: int(g_txt), b: int(b_txt)}
         else split_rgb_second(digits, r_txt, start, g_len - 1))
    else split_rgb_second(digits, r_txt, start, g_len - 1)
}

fn valid_byte_text(text) {
    (len(text) > 0) and (int(text) >= 0) and (int(text) <= 255)
}

fn hex_byte(value) {
    let hi = floor(float(value) / 16.0)
    let lo = value - int(hi) * 16
    hex_digit(int(hi)) ++ hex_digit(lo)
}

fn hex_digit(n) {
    if (n == 0) "0" else if (n == 1) "1" else if (n == 2) "2" else if (n == 3) "3"
    else if (n == 4) "4" else if (n == 5) "5" else if (n == 6) "6" else if (n == 7) "7"
    else if (n == 8) "8" else if (n == 9) "9" else if (n == 10) "a" else if (n == 11) "b"
    else if (n == 12) "c" else if (n == 13) "d" else if (n == 14) "e" else "f"
}
