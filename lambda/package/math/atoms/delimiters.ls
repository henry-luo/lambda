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

import box: lambda.package.math.box
import css: lambda.package.math.css
import met: lambda.package.math.metrics
import util: lambda.package.math.util

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
    '(': true, ')': true,
    '[': true, ']': true,
    '{': true, '}': true,
    '\\{': true, '\\}': true,
    '\\lbrace': true, '\\rbrace': true,
    '\\lbrack': true, '\\rbrack': true,
    '|': true, '\\vert': true,
    '∥': true, '\\Vert': true, '\\|': true, '\\lVert': true, '\\rVert': true,
    '\\lvert': true, '\\rvert': true,
    '\\lfloor': true, '\\rfloor': true,
    '\\lceil': true, '\\rceil': true,
    '⟨': true, '⟩': true,
    '\\langle': true, '\\rangle': true,
    '/': true, '\\backslash': true,
    '\\uparrow': true, '\\downarrow': true, '\\updownarrow': true,
    '\\Uparrow': true, '\\Downarrow': true, '\\Updownarrow': true,
    '\\lgroup': true, '\\rgroup': true,
    '\\lmoustache': true, '\\rmoustache': true,
    '\\surd': true,
    '\\ulcorner': true, '\\urcorner': true,
    '\\llcorner': true, '\\lrcorner': true
}

// Map command names to display characters
let DELIM_CHARS = {
    '<': "⟨", '>': "⟩",
    '\\{': "{", '\\}': "}",
    '\\lbrace': "{", '\\rbrace': "}",
    '\\lbrack': "[", '\\rbrack': "]",
    '\\vert': "∣", '\\lvert': "∣", '\\rvert': "∣",
    '\\|': "∥", '\\Vert': "∥", '\\lVert': "∥", '\\rVert': "∥",
    '\\lfloor': "⌊", '\\rfloor': "⌋",
    '\\lceil': "⌈", '\\rceil': "⌉",
    '\\langle': "⟨", '\\rangle': "⟩",
    '\\uparrow': "↑", '\\downarrow': "↓", '\\updownarrow': "↕",
    '\\Uparrow': "⇑", '\\Downarrow': "⇓", '\\Updownarrow': "⇕",
    '\\lgroup': "⟮", '\\rgroup': "⟯",
    '\\lmoustache': "⎰", '\\rmoustache': "⎱",
    '\\surd': "√",
    '\\ulcorner': "┌", '\\urcorner': "┐",
    '\\llcorner': "└", '\\lrcorner': "┘",
    '\\backslash': "∖"
}

// SVG path data for delimiter segments (top, repeat, bottom, middle)
// These are simplified versions of the KaTeX SVG delimiters
let SVG_DELIM_DATA = {
    '(': {
        top: "M702 0H394v44H702v-44z",
        rpt: "M702 0H394z",
        bot: "M702 0H394v44H702v-44z",
        ext: true
    },
    ')': {
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
        render_null_delim(atom_type)
    } else {
        let display_char = stretchy_char(delim)
        let level = select_size_level(content_height)
        if (is_corner_delim(display_char))
            render_corner(display_char, atom_type)
        else if (level == 3 and is_mult_left_right_delim(delim))
            render_mult_left_right_delim(delim, atom_type)
        else if (level <= 4)
            render_sized(display_char, level, atom_type)
        else
            render_svg_delim(display_char, content_height, atom_type)
    }
}

// Render a \left/\right delimiter. TeX does not size these directly from
// height + depth; it computes a minimum delimiter height around the math axis.
pub fn render_left_right(delim, height, depth, atom_type) {
    if (delim == null or delim == "" or delim == ".") {
        render_null_delim(atom_type)
    } else {
        let target_height = left_right_target_height(height, depth)
        render_stretchy(delim, target_height, atom_type)
    }
}

// Render a delimiter at a specific size (for \big, \Big, etc.)
// scale: the size factor (1.2, 1.8, 2.4, 3.0)
pub fn render_at_scale(delim, scale, atom_type) {
    if (delim == null or delim == "" or delim == ".") {
        render_null_delim(atom_type)
    } else {
        let display_char = resolve_char(delim)
        let cls = scale_to_class(scale)
        let level = scale_to_level(scale)
        if (is_vertical_bar(display_char))
            render_vertical_mult(display_char, level, atom_type)
        else (
            let h = scale * 0.5,
            let d = if (level == 1) 0.345 else scale * 0.3,
            box.make_box(
                sized_delim_el(cls, display_char, atom_type),
                h, d, 0.4 * scale, atom_type
            )
        )
    }
}

// ============================================================
// Sized font rendering (levels 1-4)
// ============================================================

fn render_sized(ch, level, atom_type) {
    let cls = level_to_class(level)
    let scale = level_to_scale(level)
    let h = if (level == 3) 1.45 else scale * 0.5
    let d = if (level == 3) 0.95 else scale * 0.3
    let raw = sized_delim_raw(level)
    if (is_vertical_bar(ch))
        render_vertical_mult(ch, level, atom_type)
    else
        delim_box(
            sized_delim_el(cls, ch, atom_type),
            h, d, 0.4 * scale, atom_type, raw.h, raw.d
        )
}

// Full-precision glyph extent of a Size1–4 delimiter (KaTeX_SizeN-Regular,
// U+0028 metrics; the Size fonts are axis-centred so every bracket shares
// these): used so the \left..\right strut rounds h+d ONCE via CEIL@2 instead
// of summing the pre-rounded height/depth (which dropped the trailing 0.00003
// and forced the old `2.41` magic).
fn sized_delim_raw(level) {
    let h = if (level == 1) 0.85 else if (level == 2) 1.15 else if (level == 3) 1.45 else 1.75
    let d = if (level == 1) 0.35001 else if (level == 2) 0.65002 else if (level == 3) 0.95003 else 1.25003
    let r = {h: h, d: d}
    r
}

fn delim_box(el, h, d, w, atom_type, h_raw, d_raw) => {
    element: el,
    height: h,
    depth: d,
    height_raw: h_raw,
    depth_raw: d_raw,
    width: w,
    type: atom_type,
    italic: 0.0,
    skew: 0.0
}

fn sized_delim_el(cls, ch, atom_type) {
    let side = side_class(atom_type)
    let all_cls = css.classes([side, cls])
    <span class: all_cls; ch>
}

fn render_null_delim(atom_type) {
    let side = side_class(atom_type)
    let cls = css.classes([css.NULLDELIMITER, side])
    {
        element: <span class: cls, style: "width:0.12em">,
        height: 0.0,
        depth: 0.0,
        width: 0.12,
        type: atom_type,
        italic: 0.0,
        skew: 0.0
    }
}

fn side_class(atom_type) {
    if (atom_type == "mopen") css.OPEN
    else if (atom_type == "mclose") css.CLOSE
    else null
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

fn scale_to_level(scale) {
    if (scale <= SIZE_1) 1
    else if (scale <= SIZE_2) 2
    else if (scale <= SIZE_3) 3
    else 4
}

fn is_vertical_bar(ch) {
    ch == "∣" or ch == "∥"
}

pub fn is_corner_delim(ch) {
    ch == "┌" or ch == "┐" or ch == "└" or ch == "┘"
}

pub fn is_surd_delim(ch) {
    ch == "√"
}

pub fn render_corner(ch, atom_type) {
    let side = side_class(atom_type)
    let cls = css.classes([css.SMALL_DELIM, side])
    {
        element: <span class: cls, style: "top:0.08em;font-size: 70%"; ch>,
        height: 0.65,
        depth: 0.15,
        render_height: 0.65,
        render_depth: 0.15,
        render_total: 0.8,
        width: 0.4,
        type: atom_type,
        italic: 0.0,
        skew: 0.0
    }
}

fn render_vertical_mult(ch, level, atom_type) {
    // vlist internals (piece tops, container height/depth, pstrut, glyph
    // height, total) are computed by a faithful makeStackedDelim port; the
    // strut box fields (height/depth/raw/total) stay axis-centred via
    // sized_delim_raw so the use_raw emit and the math.ls depth sentinel
    // (total\u22481.21, depth\u22480.345) keep firing.
    let spec = make_stacked_delim(level)
    let pieces = stk_pieces(spec, ch, 0, [])
    let raw = sized_delim_raw(level)
    let cls = css.classes([side_class(atom_type), "lm_delim-mult"])
    {
        element: <span class: cls;
            <span class: "delim-size1 lm_vlist-t lm_vlist-t2";
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(spec.vlist_h);
                        for (piece in pieces) piece
                    >
                    <span class: css.VLIST_S; "\u200B">
                >
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(spec.depth_holder)>
                >
            >
        >,
        height: raw.h,
        depth: vertical_mult_box_depth(level),
        height_raw: raw.h,
        depth_raw: raw.d,
        render_height: raw.h,
        render_depth: vertical_mult_box_depth(level),
        render_total: util.ceil_em2(spec.real_h),
        width: 0.4,
        type: atom_type,
        italic: 0.0,
        skew: 0.0
    }
}

fn is_mult_left_right_delim(delim) {
    delim == "\\uparrow" or delim == "\\downarrow" or
    delim == "\\updownarrow" or delim == "\\Uparrow" or
    delim == "\\Downarrow" or delim == "\\Updownarrow" or
    delim == "\\lgroup" or delim == "\\rgroup" or
    delim == "\\lmoustache" or delim == "\\rmoustache"
}

fn render_mult_left_right_delim(delim, atom_type) {
    let spec = mult_left_right_spec(delim)
    let pieces = mult_left_right_pieces(spec, 0, [])
    let cls = css.classes([side_class(atom_type), "lm_delim-mult"])
    {
        element: <span class: cls;
            <span class: spec.inner_class;
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:1.44em";
                        for (piece in pieces) piece
                    >
                    <span class: css.VLIST_S; "\u200B">
                >
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:0.96em">
                >
            >
        >,
        height: 1.45,
        depth: 0.95,
        height_raw: 1.45,
        depth_raw: 0.95003,
        render_height: 1.45,
        render_depth: 0.95,
        render_total: 2.41,
        width: 0.4,
        type: atom_type,
        italic: 0.0,
        skew: 0.0
    }
}

fn mult_left_right_pieces(spec, i, acc) {
    if (i >= len(spec.chars)) acc
    else
        (let piece = <span style: "top:" ++ util.fmt_em(spec.tops[i]);
             <span class: css.PSTRUT, style: "height:" ++ util.fmt_em(spec.pstrut)>
             <span style: "height:" ++ util.fmt_em(spec.heights[i]) ++ ";display:inline-block"; spec.chars[i]>
         >,
         mult_left_right_pieces(spec, i + 1, acc ++ [piece]))
}

fn mult_left_right_spec(delim) {
    if (delim == "\\downarrow")
        arrow_down_spec("⏐")
    else if (delim == "\\Downarrow")
        arrow_down_spec("‖")
    else if (delim == "\\uparrow")
        arrow_up_spec("⏐")
    else if (delim == "\\Uparrow")
        arrow_up_spec("‖")
    else if (delim == "\\updownarrow")
        arrow_updown_spec(["↓", "⏐", "⏐", "↑"])
    else if (delim == "\\Updownarrow")
        arrow_updown_spec(["⇓", "‖", "‖", "⇑"])
    else if (delim == "\\lgroup")
        brace_group_spec(["⎩", "⎪", "⎪", "⎧"])
    else if (delim == "\\rgroup")
        brace_group_spec(["⎭", "⎪", "⎪", "⎫"])
    else if (delim == "\\lmoustache")
        brace_group_spec(["⎭", "⎪", "⎪", "⎧"])
    else if (delim == "\\rmoustache")
        brace_group_spec(["⎩", "⎪", "⎪", "⎫"])
    else arrow_down_spec("⏐")
}

fn arrow_down_spec(ext) => {
    inner_class: "delim-size1 lm_vlist-t lm_vlist-t2",
    chars: ["\\\\", ext, ext],
    tops: [0.0 - 2.24, 0.0 - 3.09, 0.0 - 3.68],
    heights: [1.21, 0.61, 0.61],
    pstrut: 2.85
}

fn arrow_up_spec(ext) => {
    inner_class: "delim-size1 lm_vlist-t lm_vlist-t2",
    chars: [ext, ext, "\\\\"],
    tops: [0.0 - 1.89, 0.0 - 2.49, 0.0 - 3.43],
    heights: [0.61, 0.61, 1.21],
    pstrut: 2.85
}

fn arrow_updown_spec(chars) => {
    inner_class: "delim-size1 lm_vlist-t lm_vlist-t2",
    chars: chars,
    tops: [0.0 - 1.65, 0.0 - 2.24, 0.0 - 2.84, 0.0 - 3.43],
    heights: [0.61, 0.61, 0.61, 0.61],
    pstrut: 2.61
}

fn brace_group_spec(chars) => {
    inner_class: "delim-size4 lm_vlist-t lm_vlist-t2",
    chars: chars,
    tops: [0.0 - 2.84, 0.0 - 2.84, 0.0 - 3.14, 0.0 - 3.43],
    heights: [0.91, 0.3, 0.3, 0.91],
    pstrut: 2.9
}

// ============================================================
// makeStackedDelim port (vertical bars) — MathLive core/delimiters.ts
// makeStackedDelim + core/v-box.ts makeRows ("bottom" mode).
//
// A vertical bar stacks a top + repeats + bottom of the SAME Size1 glyph
// (∣ U+2223 / ∥ U+2225 share metrics), centred on the math axis, then
// lays them out with the makeVList "bottom" walk. Every emitted dimension
// is CEIL@2 (math.ls util / MathLive Box.toString). This replaces the old
// per-level lookup tables (tops/height/depth_holder/render_total + the
// hardcoded 2.61 pstrut / 0.61 glyph) with the computation that produced
// them.
// ============================================================

// Size1-Regular extent shared by U+2223 (∣) and U+2225 (∥). The Size fonts
// are not in metrics_data, so the value is cited here (KaTeX_Size1-Regular).
let BAR1_H = 0.606
let BAR1_D = 0.0 - 0.00599
let STK_OVERLAP = 0.008          // OVERLAP between segments, in em

// \big/\Big/\bigg/\Bigg size buckets → makeSizedDelim sizeToMaxHeight
fn size_to_max_height(level) {
    if (level == 1) 1.2 else if (level == 2) 1.8 else if (level == 3) 2.4 else 3.0
}

// integer ceil of a (possibly slightly negative) quotient — repeatCount
fn ceil_int(q) {
    let i = int(q)
    if (float(i) >= q) i else i + 1
}

// the VBox child list: bottom, -OVERLAP, repeat*rc, -OVERLAP, top
// (box entries = {box:true}, kern entries = {kern:value})
fn stk_repeats(n, acc) {
    if (n <= 0) acc else stk_repeats(n - 1, acc ++ [{box: true}])
}
fn stk_entries(rc) {
    let head = [{box: true}, {kern: 0.0 - STK_OVERLAP}]
    let tail = [{kern: 0.0 - STK_OVERLAP}, {box: true}]
    head ++ stk_repeats(rc, []) ++ tail
}

// makeVList "bottom" walk: thread currPos, track maxPos/minPos and each
// box's `top` offset (top = -pstrut - currPos - glyphDepth)
fn stk_walk(entries, i, h, d, pstrut, cur, mx, mn, tops) {
    if (i >= len(entries)) {
        let r = {maxPos: mx, minPos: mn, tops: tops}
        r
    }
    else {
        let e = entries[i]
        if (e.box == true) {
            let top = 0.0 - pstrut - cur - d
            let nc = cur + h + d
            stk_walk(entries, i + 1, h, d, pstrut, nc, max(mx, nc), min(mn, nc), tops ++ [top])
        } else {
            let nc = cur + e.kern
            stk_walk(entries, i + 1, h, d, pstrut, nc, max(mx, nc), min(mn, nc), tops)
        }
    }
}

// full makeStackedDelim for a vertical bar at a given \big size level.
// The glyph metrics are read from the module-level float constants here
// (rather than passed in) so the transpiler infers them as float — passing
// them as params makes the first use `h + d` ambiguous and mis-infer int.
fn make_stacked_delim(level) {
    let h = BAR1_H
    let d = BAR1_D
    let ht = size_to_max_height(level)
    let rht = h + d
    let min_height = 2.0 * rht
    let rc = max(0, ceil_int((ht - min_height) / rht))
    let real_h = min_height + (float(rc) * rht)
    let depth_param = real_h / 2.0 - met.AXIS_HEIGHT
    let pstrut = h + 2.0
    let bottom = 0.0 - depth_param
    let w = stk_walk(stk_entries(rc), 0, h, d, pstrut, bottom, bottom, bottom, [])
    {
        tops: w.tops,
        vlist_h: w.maxPos,
        depth_holder: 0.0 - w.minPos,
        pstrut: pstrut,
        glyph_h: rht,
        real_h: real_h
    }
}

// emit the vlist pieces (one per stacked glyph) from a computed spec
fn stk_pieces(spec, ch, i, acc) {
    if (i >= len(spec.tops)) acc
    else
        (let piece = <span style: "top:" ++ util.fmt_em_ceil2(spec.tops[i]);
             <span class: css.PSTRUT, style: "height:" ++ util.fmt_em_ceil2(spec.pstrut)>
             <span style: "height:" ++ util.fmt_em_ceil2(spec.glyph_h) ++ ";display:inline-block"; ch>
         >,
         stk_pieces(spec, ch, i + 1, acc ++ [piece]))
}

// strut depth field — kept as a small table because math.ls couples the
// level-1 value (0.345) to its depth sentinel (total≈1.21 → "-0.35em")
fn vertical_mult_box_depth(level) {
    if (level == 1) 0.345 else if (level == 2) 0.65 else if (level == 3) 0.95 else 1.25
}

// ============================================================
// SVG-based stretchy rendering (level 5+)
// ============================================================

fn render_svg_delim(ch, target_height, atom_type) {
    // Beyond Size4, use Size4 at native font-size.
    // (SVG-based stretchy delimiters are not yet supported;
    //  font-size scaling is unreliable due to em cascade issues.)
    render_sized(ch, 4, atom_type)
}

fn left_right_target_height(height, depth) {
    let axis = met.AXIS_HEIGHT
    let max_dist = max(height - axis, depth + axis)
    let factor_target = max_dist * 901.0 / 500.0
    let shortfall_target = 2.0 * max_dist - 5.0 / met.PT_PER_EM
    max(factor_target, shortfall_target)
}

// ============================================================
// Helpers
// ============================================================

// resolve a delimiter command to its display character
pub fn resolve_char(delim) {
    let explicit = resolve_escaped_char(delim)
    if (explicit != null) explicit
    else if (delim == "|") "∣"
    else
        (let mapped = DELIM_CHARS[delim],
         if (mapped != null) mapped else delim)
}

fn resolve_escaped_char(delim) {
    if (is_backslash_prefixed(delim))
        resolve_command_delim(slice(delim, 1, len(delim)))
    else null
}

fn is_backslash_prefixed(delim) {
    (len(delim) > 1) and slice(delim, 0, 1) == "\\"
}

fn resolve_command_delim(name) {
    if (name == "{") "{"
    else if (name == "}") "}"
    else if (name == "lbrace") "{"
    else if (name == "rbrace") "}"
    else if (name == "lbrack") "["
    else if (name == "rbrack") "]"
    else if (name == "vert") "∣"
    else if (name == "lvert") "∣"
    else if (name == "rvert") "∣"
    else if (name == "|") "∣"
    else if (name == "Vert") "∥"
    else if (name == "lVert") "∥"
    else if (name == "rVert") "∥"
    else if (name == "lfloor") "⌊"
    else if (name == "rfloor") "⌋"
    else if (name == "lceil") "⌈"
    else if (name == "rceil") "⌉"
    else if (name == "langle") "⟨"
    else if (name == "rangle") "⟩"
    else if (name == "uparrow") "↑"
    else if (name == "downarrow") "↓"
    else if (name == "updownarrow") "↕"
    else if (name == "Uparrow") "⇑"
    else if (name == "Downarrow") "⇓"
    else if (name == "Updownarrow") "⇕"
    else if (name == "lgroup") "⟮"
    else if (name == "rgroup") "⟯"
    else if (name == "lmoustache") "⎰"
    else if (name == "rmoustache") "⎱"
    else if (name == "surd") "√"
    else if (name == "ulcorner") "┌"
    else if (name == "urcorner") "┐"
    else if (name == "llcorner") "└"
    else if (name == "lrcorner") "┘"
    else if (name == "backslash") "∖"
    else null
}

fn stretchy_char(delim) {
    if (delim == "\\|") "∥"
    else resolve_char(delim)
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
