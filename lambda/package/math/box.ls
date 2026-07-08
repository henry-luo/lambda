// math/box.ls — Box model for math rendering
// A box is a map: {element, height, depth, width, type, classes, italic, skew}
// Boxes wrap Lambda elements (<span>) and carry metric metadata.

import css: .css
import util: .util
import met: .metrics
import metrics_data: .metrics_data

// ============================================================
// Box constructors
// ============================================================

// create a box from an element with specified metrics
pub fn make_box(el, height, depth, width, box_type) =>
    ml_box(el, height, depth, width, box_type)

// create a MathLive-model box. Its height/depth are full-precision layout
// dimensions; visual CSS extents must live in the element tree, not in render_*
// side channels. The `model` marker lets Phase A migrate producers one by one.
pub fn ml_box(el, height, depth, width, box_type) => {
    element: el,
    height: height,
    depth: depth,
    width: width,
    type: box_type,
    italic: 0.0,
    skew: 0.0,
    max_font_size: height,
    model: "ml"
}

pub fn ml_box_full(el, height, depth, width, box_type, italic, skew, max_font_size) => {
    element: el,
    height: height,
    depth: depth,
    width: width,
    type: box_type,
    italic: italic,
    skew: skew,
    max_font_size: max_font_size,
    model: "ml"
}

pub fn is_ml_box(bx) {
    bx != null and bx.model == "ml"
}

fn all_ml_boxes(items, i) {
    if (i >= len(items)) true
    else if (not is_ml_box(items[i])) false
    else all_ml_boxes(items, i + 1)
}

// create a box with a <span class=cls> element (constructed internally)
pub fn box_cls(cls, height, depth, width, box_type) =>
    ml_box_full(<span class: cls>, height, depth, width, box_type, 0.0, 0.0, height)

// create a box with <span class=cls style=style> element
pub fn box_styled(cls, style, height, depth, width, box_type) =>
    ml_box_full(<span class: cls, style: style>, height, depth, width, box_type, 0.0, 0.0, height)

// create a box from a text string (leaf node). The element keeps `text`
// verbatim (which may carry the U+E000/U+E001 `<`/`>` raw-emit sentinels),
// while metric lookups use the de-sentinelized form so heights/widths come
// from the real `<`/`>` glyph metrics.
pub fn text_box(text, cls, box_type) {
    let mt = metric_text(text)
    let h = text_height_for(mt, cls)
    let d = text_depth_for(mt, cls)
    let h_exact = text_height_exact_for(mt, cls)
    let d_exact = text_depth_exact_for(mt, cls)
    {
        element: text_element(text, cls),
        height: if (h_exact != null) h_exact else h,
        depth: if (d_exact != null) d_exact else d,
        width: met.DEFAULT_CHAR_WIDTH * float(len(text)),
        type: box_type,
        italic: 0.0,
        skew: 0.0,
        max_font_size: if (h_exact != null) h_exact else h,
        model: "ml"
    }
}

// Map the raw-emit sentinels back to their real glyphs for metric lookup.
fn metric_text(text) {
    if (text == "\u{E000}") "<"
    else if (text == "\u{E001}") ">"
    else text
}

// Full-precision height for strut emission. Looks up the metric
// table for both single-char and multi-char text. For +/−/-/ı/ȷ we still
// keep the rounded `height` at 0.69 to avoid cascading into fraction
// constants, but we ALSO expose the truthful 0.58333 here so the outer
// strut can emit the correct h+d sum.
fn text_height_exact_for(text, cls) {
    let normalized = if (text == "-") "−" else text
    if (len(normalized) == 1) {
        let font = font_from_class(cls)
        let m = if (font != null) metrics_data.lookup(normalized, font) else null
        if (m != null) metrics_data.height_exact_of(m) else null
    }
    else if (is_alpha_multi(normalized) and font_from_class(cls) != null)
        max_char_height_exact(normalized, font_from_class(cls))
    else null
}

fn text_depth_exact_for(text, cls) {
    let normalized = if (text == "-") "−" else text
    if (len(normalized) == 1) {
        let font = font_from_class(cls)
        let m = if (font != null) metrics_data.lookup(normalized, font) else null
        if (m != null) metrics_data.depth_exact_of(m) else null
    }
    else if (is_alpha_multi(normalized) and font_from_class(cls) != null)
        max_char_depth_exact(normalized, font_from_class(cls))
    else null
}

fn max_char_height_exact(text, font) {
    max_char_h_exact_loop(text, font, 0, 0.0, true)
}

fn max_char_h_exact_loop(text, font, i, acc, found_any) {
    if (i >= len(text)) {
        if (found_any) acc else null
    } else {
        let m = metrics_data.lookup(text[i], font)
        let h = if (m != null) metrics_data.height_exact_of(m) else null
        if (h == null) null
        else if (h > acc) max_char_h_exact_loop(text, font, i + 1, h, true)
        else max_char_h_exact_loop(text, font, i + 1, acc, true)
    }
}

fn max_char_depth_exact(text, font) {
    max_char_d_exact_loop(text, font, 0, 0.0, true)
}

fn max_char_d_exact_loop(text, font, i, acc, found_any) {
    if (i >= len(text)) {
        if (found_any) acc else null
    } else {
        let m = metrics_data.lookup(text[i], font)
        let d = if (m != null) metrics_data.depth_exact_of(m) else null
        if (d == null) null
        else if (d > acc) max_char_d_exact_loop(text, font, i + 1, d, true)
        else max_char_d_exact_loop(text, font, i + 1, acc, true)
    }
}

// Look up metrics from MathLive's character metrics table when possible,
// falling back to the heuristic text_height/text_depth.
fn font_from_class(cls) {
    if (cls == css.CMR) "cmr"
    else if (cls == css.MATHIT) "mathit"
    else if (cls == css.AMS) "ams"
    else if (cls == css.BB) "ams"
    else if (cls == css.MATHBF) "mathbf"
    else if (cls == css.TT) "tt"
    else if (cls == css.FRAK) "frak"
    else if (cls == css.SCRIPT) "script"
    else if (cls == css.CAL) "cal"
    else if (cls == css.SANS) "sans"
    else if (cls == "lcGreek lm_mathit") "mathit"
    else if (cls == "lm_cmr lm_it") "mathit"
    else null
}

fn text_height_for(text, cls) {
    // Operators (+/−) cascade into nested fraction/box height calcs that
    // were calibrated against Lambda's old 0.69 height. Some symbols (ı, ȷ)
    // are rendered with compound classes that MathLive picks from a font
    // we don't have metrics for. Skip lookup for these.
    if (text == "+" or text == "−" or text == "-" or
        text == "ı" or text == "ȷ") text_height(text)
    else if (len(text) == 1) {
        let font = font_from_class(cls)
        let m = if (font != null) metrics_data.lookup(text, font) else null
        let h = if (m != null) metrics_data.height_of(m) else null
        if (h != null and h > 0.0) h else text_height(text)
    } else if (is_alpha_multi(text) and font_from_class(cls) != null)
        // Multi-char alpha (operator names like sin/cos/arcsin/sinh): take
        // the max per-character height from the font metric table. Falls
        // back to the heuristic if any char is unknown.
        max_char_height(text, font_from_class(cls), text_height(text))
    else if (is_alpha_multi(text) and font_from_class(cls) == null)
        // No font metric (e.g., mathtt/mathfrak/mathsf/mathcal/mathscr).
        // Approximate with cmr letter metrics — much closer than the 0.7em
        // heuristic fallback. Lowercase "code" yields ~0.62em, matches MathLive.
        max_char_height(text, "cmr", text_height(text))
    else if (is_repeated_char(text) and font_from_class(cls) != null) {
        // Repeated identical char like "..." or "---": use that char's metric.
        let font = font_from_class(cls)
        let m = metrics_data.lookup(text[0], font)
        let h = if (m != null) metrics_data.height_of(m) else null
        if (h != null and h > 0.0) h else text_height(text)
    } else if (is_digit_text_full(text) and font_from_class(cls) != null) {
        // Numeric strings like "3.14" or "1000": max per-char height. Digits
        // share a 0.65em cap-height in cmr; the `.` and `,` are short, so the
        // overall max ≈ 0.65 — much closer to MathLive than the 0.7 heuristic.
        max_char_height(text, font_from_class(cls), text_height(text))
    } else text_height(text)
}

fn is_digit_text_full(text) {
    if (len(text) <= 1) false
    else is_digit_chars(text, 0)
}

fn is_digit_chars(text, i) {
    if (i >= len(text)) true
    else (let ch = text[i],
        if ((ch >= "0" and ch <= "9") or ch == "." or ch == ",") is_digit_chars(text, i + 1)
        else false)
}

fn is_repeated_char(text) {
    if (len(text) <= 1) false
    else is_repeated_char_at(text, 1, text[0])
}

fn is_repeated_char_at(text, i, ch) {
    if (i >= len(text)) true
    else if (text[i] == ch) is_repeated_char_at(text, i + 1, ch)
    else false
}

fn is_alpha_multi(text) {
    if (len(text) > 1) is_alpha_chars(text, 0) else false
}

fn is_alpha_chars(text, i) {
    if (i >= len(text)) true
    else {
        let ch = text[i]
        let lo = (ch >= "a") and (ch <= "z")
        let up = (ch >= "A") and (ch <= "Z")
        if (lo or up) is_alpha_chars(text, i + 1) else false
    }
}

fn max_char_height(text, font, fallback) {
    max_char_height_loop(text, font, 0, 0.0, fallback)
}

fn max_char_height_loop(text, font, i, acc, fallback) {
    if (i >= len(text)) acc
    else {
        let ch = text[i]
        let m = metrics_data.lookup(ch, font)
        let h = if (m != null) metrics_data.height_of(m) else null
        if (h == null) fallback
        else if (h > acc) max_char_height_loop(text, font, i + 1, h, fallback)
        else max_char_height_loop(text, font, i + 1, acc, fallback)
    }
}

fn text_depth_for(text, cls) {
    if (len(text) == 1) {
        let font = font_from_class(cls)
        let m = if (font != null) metrics_data.lookup(text, font) else null
        let d = if (m != null) metrics_data.depth_of(m) else null
        if (d != null) d else text_depth(text)
    } else text_depth(text)
}

fn text_element(text, cls) {
    let style = text_style(text, cls)
    if (cls and style != null) {
        <span class: cls, style: style; text>
    } else if (cls) {
        <span class: cls; text>
    } else if (style != null) {
        <span style: style; text>
    } else {
        <span; text>
    }
}

fn text_style(text, cls) {
    // Derive italic correction (margin-right) from metrics_data for single
    // characters. metrics_data stores italic values already rounded to
    // CEIL@2, matching MathLive's toString() emission rule. Italic correction
    // applies based on the SOURCE FONT — both mathit (cmmi) letters and a
    // small set of cmr symbols (∂, ∫, V/W/Y, _, f/g/v/w/y) carry italic
    // metadata. Greek letters that ship under the compound class go through
    // the mathit table.
    if (len(text) == 1 and (cls == css.MATHIT or cls == "lcGreek lm_mathit")) {
        let m = metrics_data.lookup(text, "mathit")
        let it = if (m != null) metrics_data.italic_of(m) else null
        if (it != null and it > 0.0) "margin-right:" ++ util.fmt_em(it) else null
    }
    else if (len(text) == 1 and cls == css.CMR) {
        let m = metrics_data.lookup(text, "cmr")
        let it = if (m != null) metrics_data.italic_of(m) else null
        if (it != null and it > 0.0) "margin-right:" ++ util.fmt_em(it) else null
    }
    else if (len(text) == 1 and italic_corrected_font(cls) != null) {
        // Script/Caligraphic/Fraktur capitals carry an italic correction
        // (e.g. \mathscr{F} → margin-right:0.14em) emitted from the
        // per-font metric table.
        let m = metrics_data.lookup(text, italic_corrected_font(cls))
        let it = if (m != null) metrics_data.italic_of(m) else null
        if (it != null and it > 0.0) "margin-right:" ++ util.fmt_em(it) else null
    }
    else null
}

fn italic_corrected_font(cls) {
    if (cls == css.MATHBF) "mathbf"
    else if (cls == css.SCRIPT) "script"
    else if (cls == css.CAL) "cal"
    else if (cls == css.FRAK) "frak"
    else null
}

fn text_has_tall_delim(text) {
    contains(text, "(") or contains(text, ")") or
    contains(text, "[") or contains(text, "]") or
    contains(text, "{") or contains(text, "}") or
    contains(text, "|") or
    // Unicode delimiters and operators with tall (~0.75em) cmsy heights
    contains(text, "⟨") or contains(text, "⟩") or  // langle/rangle
    contains(text, "∣") or contains(text, "∥") or  // mid/parallel
    contains(text, "‖") or                          // Vert
    contains(text, "⌊") or contains(text, "⌋") or  // lfloor/rfloor
    contains(text, "⌈") or contains(text, "⌉") or  // lceil/rceil
    contains(text, "∅") or                          // emptyset
    contains(text, "∖")                             // setminus
}

fn text_height(text) {
    // Latin/greek letter heights (lower + UPPER) now come from metrics_data
    // (Math-Italic / Main-Regular); those per-letter heuristics were dead —
    // text_height_for() looks up metrics_data first. Most per-symbol height
    // heuristics were likewise dead (cmr/ams/mathit classes the tables cover).
    // Only render paths that BYPASS the lookup remain below — context-font
    // fallbacks (nleq/ngeq/diagonal arrows resolve to the ambient mathit, which
    // lacks these glyphs), bare punctuation, the +/− skip, and ASCII </> .
    if (text == "▽") 0.55
    else if (text == ".") 0.11
    else if (text == ",") 0.11
    else if (text == ":") 0.44
    else if (text == "≰" or text == "≱") 0.8
    else if (text == "≮" or text == "≯") 0.71
    else if (text == "•") 0.45
    else if (text == "†" or text == "‡") 0.7
    else if (text == "⋄") 0.45
    else if (text == "ℸ") 0.69
    else if (text == "+" or text == "−") 0.69
    else if (text == "<" or text == ">") 0.64
    // Diagonal arrows resolve to the ambient (mathit) font, not covered
    else if (text == "↗" or text == "↖" or text == "↘" or text == "↙") 0.37
    else if (is_number_text(text)) 0.65
    else if (text_has_tall_delim(text)) 0.75
    // Multi-char operator names (sin/cos/log/…) were DEAD here: they render as
    // text_box(name, css.CMR, "mop"), so text_height_for() takes the
    // max_char_height(name, "cmr", …) path and only uses this function as the
    // (never-reached) fallback — every composing letter is in the cmr table.
    else met.DEFAULT_CHAR_HEIGHT
}

fn text_depth(text) {
    if (text_has_tall_delim(text)) 0.25
    else if (text == "_") 0.31
    else if (text == "," or text == ";") 0.19
    // For descender letters (g/j/p/q/y/f/Q, plus multi-char strings like
    // "log"/"lim sup"), use cmmi descent ≈ 0.19444.
    else if (text_has_descender(text)) 0.19
    // Dotless i/j (cmmi M118): raw descent 0.19444 → CEIL@2 0.20 in the
    // bottom strut (single-round; matches \imath/\jmath golden 0.90 / -0.20).
    // ı/ȷ have no metrics_data entry, so this heuristic IS reached.
    else if (text == "ı" or text == "ȷ") 0.2
    // The remaining per-symbol depths now come from metrics_data; only render
    // paths that bypass the lookup stay below: ∈/∉/⊥/⊤, ≰/≱/≮/≯, daggers /
    // wreath / sqcup, and diagonal arrows resolve to the ambient (mathit)
    // font that lacks these glyphs; • is emitted bare.
    else if (text == "∈" or text == "∉" or text == "⊥" or text == "⊤") 0.2
    else if (text == "≰" or text == "≱") 0.3
    else if (text == "≮" or text == "≯") 0.2
    else if (text == "†" or text == "‡" or
             text == "⊔" or text == "⨿" or text == "≀") 0.19
    else if (text == "•") 0.0 - 0.06
    else if (text == "⋄") 0.0 - 0.06
    // Diagonal arrows (nearrow/nwarrow/searrow/swarrow) — negative depth
    else if (text == "↗" or text == "↖" or text == "↘" or text == "↙") 0.0 - 0.14
    // Binary operators that have depth in MathLive's cmr metrics
    // (+, −, ÷, ×, =, <, >, etc. have depth ≈ 0.08319).
    else if (is_operator_with_depth(text)) 0.08
    // Default is 0 — matches MathLive's cmr/cmmi/ams tables where
    // letters, most symbols, and accents have depth 0. The lm_strut--bottom
    // is then conditionally omitted by make_struts() when there's no descent.
    else 0.0
}

fn is_operator_with_depth(text) {
    // Operators whose MathLive cmr/cmsy metric has depth ≈ 0.08 (or close).
    // Operators with NEGATIVE depth (=, ≈, ≃, ≅ — all rendered above baseline)
    // are excluded — treated as depth 0.
    if (len(text) != 1) false
    else
        text == "+" or text == "−" or text == "-" or
        text == "<" or text == ">" or text == "*" or text == "/" or
        // Circled operators (cmsy) — observed MathLive output has -0.08em va
        text == "⊕" or text == "⊗" or text == "⊙" or text == "⊖" or text == "⊘"
}

fn text_has_descender(text) {
    has_descender_at(text, 0)
}

fn has_descender_at(text, i) {
    if (i >= len(text)) false
    else
        (let ch = slice(text, i, i + 1),
         if (ch == "g" or ch == "j" or ch == "p" or ch == "q" or
             ch == "y" or ch == "f" or ch == "Q") true
         else has_descender_at(text, i + 1))
}

fn is_number_text(text) {
    (len(text) > 0) and is_number_text_at(text, 0)
}

fn is_number_text_at(text, i) {
    if (i >= len(text)) true
    else
        (let ch = slice(text, i, i + 1),
         if (ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
             ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9")
            is_number_text_at(text, i + 1)
         else false)
}

// create an empty skip box (horizontal spacer)
pub fn skip_box(width_em) => {
    element: <span style: "display:inline-block;width:" ++ util.fmt_em(width_em)>,
    height: 0.0,
    depth: 0.0,
    width: width_em,
    type: "skip",
    italic: 0.0,
    skew: 0.0,
    max_font_size: 0.0,
    model: "ml"
}

// create a null delimiter box (invisible spacer with fixed width)
pub fn null_delim() {
    {
        element: <span class: css.NULLDELIMITER>,
        height: 0.0,
        depth: 0.0,
        width: 0.12,
        type: "mopen",
        italic: 0.0,
        skew: 0.0,
        max_font_size: 0.0,
        model: "ml"
    }
}

// ============================================================
// Box combinators
// ============================================================

// horizontally concatenate boxes into one
pub fn hbox(boxes) {
    let valid = (for (b in boxes where b != null) b)
    let children = collect_elements(valid, 0, [])
    let total_width = sum((for (v in valid) v.width))
    let suppress_text_depth = has_suppress_hbox_text_depth(valid, 0)
    let suppress_operator_height = has_suppress_hbox_operator_height(valid, 0)
    if (len(valid) == 0)
        ml_box(<span class: css.BASE>, 0.0, 0.0, 0.0, "ord")
    else
        ml_hbox_valid(valid, children, total_width, suppress_text_depth, suppress_operator_height)
}

// Combine MathLive-model boxes without reintroducing legacy render/raw side
// channels. This is the horizontal propagation point for the Phase A migration:
// once every child in an hbox is one-box-field, the parent remains one too.
fn ml_hbox_valid(valid, children, total_width, suppress_text_depth, suppress_operator_height) {
    ml_box_full(
        <span class: css.BASE;
            for (child in children) child
        >,
        max((for (v in valid) hbox_height_of(v, suppress_operator_height))),
        max((for (v in valid) hbox_depth_of(v, suppress_text_depth))),
        total_width,
        "ord",
        0.0,
        0.0,
        ml_hbox_max_font_size(valid, 0, 0.0, suppress_operator_height)
    )
}

fn ml_hbox_max_font_size(valid, i, acc, suppress_operator_height) {
    if (i >= len(valid)) acc
    else {
        let bx = valid[i]
        let mf0 = if (bx.max_font_size != null) bx.max_font_size else bx.height
        let mf = if (suppress_operator_height and is_binary_operator_text_box(bx))
            min(mf0, 0.65) else mf0
        ml_hbox_max_font_size(valid, i + 1, max(acc, mf), suppress_operator_height)
    }
}

pub fn child_elements(boxes) {
    let valid = (for (b in boxes where b != null) b)
    collect_elements(valid, 0, [])
}

pub fn elements_of(bx) {
    if (bx.elements != null) bx.elements
    else if (bx.element is element and bx.element.class == css.BASE)
        element_children(bx.element, 0, [])
    else [bx.element]
}

fn element_children(el, i, acc) {
    if (i >= len(el)) acc
    else element_children(el, i + 1, acc ++ [el[i]])
}

fn collect_elements(valid, i, acc) {
    if (i >= len(valid)) acc
    else
        (let v = valid[i],
         let next = acc ++ elements_of(v),
         collect_elements(valid, i + 1, next))
}

fn has_suppress_hbox_text_depth(items, i) {
    if (i >= len(items)) false
    else if (items[i].suppress_hbox_text_depth == true) true
    else has_suppress_hbox_text_depth(items, i + 1)
}

fn has_script_radical(items, i) {
    if (i >= len(items)) false
    else if (items[i].is_script_radical == true) true
    else has_script_radical(items, i + 1)
}

fn has_suppress_hbox_operator_height(items, i) {
    if (i >= len(items)) false
    else if (items[i].suppress_hbox_operator_height == true) true
    else has_suppress_hbox_operator_height(items, i + 1)
}

fn hbox_depth_of(bx, suppress_text_depth) {
    if (suppress_text_depth and is_depthless_text_box(bx)) 0.0
    else bx.depth
}

fn hbox_height_of(bx, suppress_operator_height) {
    if (suppress_operator_height and is_binary_operator_text_box(bx)) 0.65
    else bx.height
}

fn is_depthless_text_box(bx) {
    bx.element is element and len(bx.element) == 1 and
    bx.element.class == css.MATHIT and
    bx.element[0] is string
}

fn is_binary_operator_text_box(bx) {
    bx.element is element and len(bx.element) == 1 and
    bx.element.class == css.CMR and
    bx.element[0] is string and
    (string(bx.element[0]) == "+" or string(bx.element[0]) == "−")
}

// ============================================================
// VBox — vertical stacking using inline-table layout
// ============================================================

// create a vertical box with individually shifted children
// children: [{box, shift}] where shift is distance from baseline (negative = up)
// Returns a box wrapping the vertically-stacked content
// helper: build vbox internals when there are children
fn build_vbox(children) {
    let max_width = max((for (c in children) c.box.width))
    // compute bounding box: position above baseline = 0.0 - shift
    let tops = (for (c in children) (0.0 - c.shift) + c.box.height)
    let bottoms = (for (c in children) (0.0 - c.shift) - c.box.depth)
    let height = max(max(tops), 0.0)
    let depth = max(0.0 - min(bottoms), 0.0)
    let total_h = height + depth

    // position each child absolutely within a relative container
    // child CSS top = height + shift - child.box.height
    let items = (for (c in children,
                      let ct = height + c.shift - c.box.height)
        <span style: "position:absolute;top:" ++ util.fmt_em(ct) ++ ";left:0;width:100%;text-align:center";
            c.box.element
        >
    )

    // inline-block baseline = bottom edge (no in-flow content);
    // vertical-align: -depth puts bottom at depth below parent baseline
    let va = util.fmt_em(0.0 - depth)
    let vbox_style = "display:inline-block;position:relative;vertical-align:" ++ va ++
                     ";width:" ++ util.fmt_em(max_width) ++
                     ";height:" ++ util.fmt_em(total_h)

    let vbox_el = <span style: vbox_style;
        for (item in items) item
    >
    {
        element: vbox_el,
        height: height,
        depth: depth,
        width: max_width,
        type: "ord",
        italic: 0.0,
        skew: 0.0,
        max_font_size: height,
        model: "ml"
    }
}

pub fn vbox(children) {
    if (len(children) == 0) {
        make_box(<span>, 0.0, 0.0, 0.0, "ord")
    } else {
        build_vbox(children)
    }
}

// ============================================================
// MathLive-style VList — migration helper
// ============================================================

// Build a MathLive-compatible VBox from individually shifted children.
// Each child is `{box, shift}` plus optional `classes`, `style`,
// `margin_left`, and `margin_right`. The shift convention matches MathLive:
// positive shift moves the child down; negative shift moves it up.
pub fn ml_vlist_individual(children, box_type) {
    if (len(children) == 0) ml_box(<span>, 0.0, 0.0, 0.0, box_type)
    else {
        let metrics = ml_vlist_individual_metrics(children)
        let el = ml_vlist_element(metrics.items, metrics.pstrut, metrics.max_pos, metrics.min_pos)
        ml_box_full(el, metrics.height, metrics.depth, metrics.width, box_type, 0.0, 0.0, metrics.height)
    }
}

// Shared makeVList geometry for shadow checks and future producer migration.
// Keeping this separate from element construction lets atoms compare computed
// positions before switching their HTML emission to the generic helper.
pub fn ml_vlist_individual_metrics(children) {
    if (len(children) == 0) {
        {items: [], pstrut: 0.0, min_pos: 0.0, max_pos: 0.0,
         height: 0.0, depth: 0.0, width: 0.0}
    } else {
        let items = ml_vlist_positioned(children, 0, [])
        let pstrut = ml_vlist_pstrut(children, 0, 0.0) + 2.0
        let ext = ml_vlist_extents(items, 0, items[0].pos, items[0].pos)
        let width = ml_vlist_width(children, 0, 0.0)
        {items: items, pstrut: pstrut, min_pos: ext.min_pos, max_pos: ext.max_pos,
         height: ext.max_pos, depth: 0.0 - ext.min_pos, width: width}
    }
}

fn ml_vlist_positioned(children, i, acc) {
    if (i >= len(children)) acc
    else {
        let child = children[i]
        let pos = 0.0 - child.shift - child.box.depth
        ml_vlist_positioned(children, i + 1, acc ++ [{
            box: child.box,
            pos: pos,
            classes: child.classes,
            style: child.style,
            margin_left: child.margin_left,
            margin_right: child.margin_right
        }])
    }
}

fn ml_vlist_pstrut(children, i, acc) {
    if (i >= len(children)) acc
    else {
        let bx = children[i].box
        let mf = if (bx.max_font_size != null) bx.max_font_size else bx.height
        ml_vlist_pstrut(children, i + 1, max(acc, max(mf, bx.height)))
    }
}

fn ml_vlist_width(children, i, acc) {
    if (i >= len(children)) acc
    else ml_vlist_width(children, i + 1, max(acc, children[i].box.width))
}

fn ml_vlist_extents(items, i, mn, mx) {
    if (i >= len(items)) {
        let r = {min_pos: mn, max_pos: mx}
        r
    }
    else {
        let bx = items[i].box
        let p0 = items[i].pos
        let p1 = p0 + (bx.height + bx.depth)
        ml_vlist_extents(items, i + 1, min(mn, min(p0, p1)), max(mx, max(p0, p1)))
    }
}

fn ml_vlist_element(items, pstrut, max_pos, min_pos) {
    if (min_pos >= 0.0) {
        let el = <span class: css.VLIST_T;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_ml_em(max_pos);
                    for (it in items) ml_vlist_child_wrap(it, pstrut)
                >
            >
        >
        el
    }
    else {
        let el = <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_ml_em(max_pos);
                    for (it in items) ml_vlist_child_wrap(it, pstrut)
                >
                <span class: css.VLIST_S; "​">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_ml_em(0.0 - min_pos)>
            >
        >
        el
    }
}

fn ml_vlist_child_wrap(it, pstrut) {
    let style = ml_vlist_wrap_style(it, pstrut)
    if (it.classes != null) {
        let el = <span class: it.classes, style: style;
            <span class: css.PSTRUT, style: "height:" ++ util.fmt_ml_em(pstrut)>
            ml_vlist_child_body(it.box)
        >
        el
    }
    else {
        let el = <span style: style;
            <span class: css.PSTRUT, style: "height:" ++ util.fmt_ml_em(pstrut)>
            ml_vlist_child_body(it.box)
        >
        el
    }
}

fn ml_vlist_wrap_style(it, pstrut) {
    "top:" ++ util.fmt_ml_em(0.0 - pstrut - it.pos - it.box.depth) ++
    (if (it.style != null) ";" ++ it.style else "") ++
    (if (it.margin_left != null) ";margin-left:" ++ util.fmt_ml_em(it.margin_left) else "") ++
    (if (it.margin_right != null) ";margin-right:" ++ util.fmt_ml_em(it.margin_right) else "")
}

fn ml_vlist_child_body(bx) {
    <span style: "height:" ++ util.fmt_ml_em(bx.height + bx.depth) ++ ";display:inline-block";
        bx.element
    >
}

// ============================================================
// Struts — browser vertical space reservation
// ============================================================

// wrap a box's element with struts for standalone rendering
// matches MathLive's box.ts makeStruts(): bottom strut is emitted ONLY when
// depth != 0 (i.e., when the box has content below the baseline).
pub fn make_struts(bx) {
    let h = bx.height
    let d = bx.depth
    if (d != 0.0) {
        let strut_bottom_style = "height:" ++ util.fmt_em(h + d) ++ ";vertical-align:" ++ util.fmt_em(0.0 - d)
        <span;
            <span class: css.STRUT,
                  style: "height:" ++ util.fmt_em(h)>
            <span class: css.STRUT_BOTTOM,
                  style: strut_bottom_style>
            bx.element
        >
    } else {
        <span;
            <span class: css.STRUT,
                  style: "height:" ++ util.fmt_em(h)>
            bx.element
        >
    }
}

// ============================================================
// Box styling
// ============================================================

// wrap a box with a CSS class
pub fn with_class(bx, cls) => {
    element: <span class: cls; bx.element>,
    height: bx.height,
    depth: bx.depth,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: bx.model
}

// wrap a box with inline style string
pub fn with_style(bx, style_str) => {
    element: <span style: style_str; bx.element>,
    height: bx.height,
    depth: bx.depth,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: bx.model
}

// wrap a box with inline styles (font-size scaling)
pub fn with_scale(bx, scale) {
    if (scale == 1.0) bx
    else
        (let pct = (round(scale * 1000.0) / 10.0) ++ "%",
         {
            element: <span style: "font-size:" ++ pct; bx.element>,
            height: bx.height * scale,
            depth: bx.depth * scale,
            width: bx.width * scale,
            type: bx.type,
            italic: bx.italic * scale,
            skew: bx.skew * scale,
            max_font_size: if (bx.max_font_size != null) bx.max_font_size * scale else null,
            model: bx.model
         })
}

// wrap a box with a color
pub fn with_color(bx, color) {
    if (color == null) bx
    else
        (let children = elements_of(bx),
         {
        element: <span style: "color:" ++ color;
            for (child in children) child
        >,
	        height: bx.height,
	        depth: bx.depth,
	        width: bx.width,
	        type: bx.type,
        italic: bx.italic,
        skew: bx.skew,
        max_font_size: bx.max_font_size,
        model: bx.model,
        suppress_hbox_text_depth: bx.suppress_hbox_text_depth,
        is_middle_delim: bx.is_middle_delim
    })
}
