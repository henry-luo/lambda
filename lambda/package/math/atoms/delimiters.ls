// math/atoms/delimiters.ls — Extensible (stretchy) delimiter rendering
// Renders tall delimiters using sized fonts or SVG paths when content
// exceeds the standard delimiter height.
//
// MathLive uses font-based delimiters at 5 size levels, then SVG for
// anything taller. We follow the same approach:
//   1. Size 1 (1.2em) — KaTeX_Size1
//   2. Size 2 (1.8em) — KaTeX_Size2
//   3. Size 3 (2.4em) — KaTeX_Size3
//   4. Size 4 (3.0em) — KaTeX_Size4
//   5. SVG stacking — top/repeat/bottom segments for arbitrary height

import box: .lambda.package.math.box
import css: .lambda.package.math.css
import util: .lambda.package.math.util

// ============================================================
// Size thresholds (em) — matches KaTeX size fonts
// ============================================================

let SIZE_1 = 1.2
let SIZE_2 = 1.8
let SIZE_3 = 2.4
let SIZE_4 = 3.0

// ============================================================
// Delimiter character tables
// ============================================================

// Standard delimiters that have sized variants in KaTeX fonts
let SIZED_DELIMS = {
    "(": true, ")": true,
    "[": true, "]": true,
    "{": true, "}": true,
    "\\{": true, "\\}": true,
    "\\lbrace": true, "\\rbrace": true,
    "|": true, "\\vert": true,
    "‖": true, "\\Vert": true, "\\|": true,
    "\\lfloor": true, "\\rfloor": true,
    "\\lceil": true, "\\rceil": true,
    "⟨": true, "⟩": true,
    "\\langle": true, "\\rangle": true,
    "/": true, "\\backslash": true,
    "\\uparrow": true, "\\downarrow": true, "\\updownarrow": true,
    "\\Uparrow": true, "\\Downarrow": true, "\\Updownarrow": true
}

// Map command names to display characters
let DELIM_CHARS = {
    "\\{": "{", "\\}": "}",
    "\\lbrace": "{", "\\rbrace": "}",
    "\\vert": "|", "\\|": "‖", "\\Vert": "‖",
    "\\lfloor": "⌊", "\\rfloor": "⌋",
    "\\lceil": "⌈", "\\rceil": "⌉",
    "\\langle": "⟨", "\\rangle": "⟩",
    "\\uparrow": "↑", "\\downarrow": "↓", "\\updownarrow": "↕",
    "\\Uparrow": "⇑", "\\Downarrow": "⇓", "\\Updownarrow": "⇕",
    "\\backslash": "∖"
}

// SVG path data for delimiter segments (top, repeat, bottom, middle)
// These are simplified versions of the KaTeX SVG delimiters
let SVG_DELIM_DATA = {
    "(": {
        top: "M702 0H394v44H702v-44z",
        rpt: "M702 0H394z",
        bot: "M702 0H394v44H702v-44z",
        ext: true
    },
    ")": {
        top: "M308 0H0v44H308v-44z",
        rpt: "M308 0H0z",
        bot: "M308 0H0v44H308v-44z",
        ext: true
    }
}

// ============================================================
// Main public API
// ============================================================

// Render a delimiter that stretches to match content height.
// delim: the delimiter string (e.g. "(", ")", "[", "|", "\\{")
// content_height: total height of content in em (height + depth)
// atom_type: "mopen" or "mclose"
// Returns a box
pub fn render_stretchy(delim, content_height, atom_type) {
    if (delim == null or delim == "" or delim == ".") {
        box.null_delim()
    } else {
        let display_char = resolve_char(delim)
        let level = select_size_level(content_height)
        if (level <= 4)
            render_sized(display_char, level, atom_type)
        else
            render_svg_delim(display_char, content_height, atom_type)
    }
}

// Render a delimiter at a specific size (for \big, \Big, etc.)
// scale: the size factor (1.2, 1.8, 2.4, 3.0)
pub fn render_at_scale(delim, scale, atom_type) {
    if (delim == null or delim == "" or delim == ".") {
        box.null_delim()
    } else {
        let display_char = resolve_char(delim)
        let cls = scale_to_class(scale)
        let h = scale * 0.5
        let d = scale * 0.3
        box.make_box(
            <span class: cls; display_char>,
            h, d, 0.4 * scale, atom_type
        )
    }
}

// ============================================================
// Sized font rendering (levels 1-4)
// ============================================================

fn render_sized(ch, level, atom_type) {
    let cls = level_to_class(level)
    let scale = level_to_scale(level)
    let h = scale * 0.5
    let d = scale * 0.3
    box.make_box(
        <span class: cls; ch>,
        h, d, 0.4 * scale, atom_type
    )
}

fn level_to_class(level) {
    if (level == 1) css.DELIM_SIZE1
    else if (level == 2) css.DELIM_SIZE2
    else if (level == 3) css.DELIM_SIZE3
    else css.DELIM_SIZE4
}

fn level_to_scale(level) {
    if (level == 1) SIZE_1
    else if (level == 2) SIZE_2
    else if (level == 3) SIZE_3
    else SIZE_4
}

fn scale_to_class(scale) {
    if (scale <= SIZE_1) css.DELIM_SIZE1
    else if (scale <= SIZE_2) css.DELIM_SIZE2
    else if (scale <= SIZE_3) css.DELIM_SIZE3
    else css.DELIM_SIZE4
}

// ============================================================
// SVG-based stretchy rendering (level 5+)
// ============================================================

fn render_svg_delim(ch, target_height, atom_type) {
    // for SVG delimiters we use a vertical stack of the character
    // with CSS transform to stretch to the target height
    let base_height = 1.0
    let scale_y = if (target_height > base_height) target_height / base_height else 1.0
    let scale_str = "scaleY(" ++ util.fmt_num(scale_y, 3) ++ ")"
    let style_str = "display:inline-block;transform:" ++ scale_str ++
                    ";transform-origin:center;height:" ++ util.fmt_em(target_height)
    let h = target_height * 0.5 + 0.1
    let d = target_height * 0.5 - 0.1
    box.make_box(
        <span class: css.SMALL_DELIM, style: style_str; ch>,
        h, d, 0.5, atom_type
    )
}

// ============================================================
// Helpers
// ============================================================

// resolve a delimiter command to its display character
fn resolve_char(delim) {
    let mapped = DELIM_CHARS[delim]
    if (mapped != null) mapped else delim
}

// select size level based on content height
// returns 1-5 (5 = use SVG)
fn select_size_level(h) {
    if (h <= SIZE_1) 1
    else if (h <= SIZE_2) 2
    else if (h <= SIZE_3) 3
    else if (h <= SIZE_4) 4
    else 5
}
