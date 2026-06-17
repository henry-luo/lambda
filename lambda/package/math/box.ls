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
pub fn make_box(el, height, depth, width, box_type) => {
    element: el,
    height: height,
    depth: depth,
    width: width,
    type: box_type,
    italic: 0.0,
    skew: 0.0
}

// create a box with a <span class=cls> element (constructed internally)
pub fn box_cls(cls, height, depth, width, box_type) => {
    element: <span class: cls>,
    height: height,
    depth: depth,
    width: width,
    type: box_type,
    italic: 0.0,
    skew: 0.0
}

// create a box with <span class=cls style=style> element
pub fn box_styled(cls, style, height, depth, width, box_type) => {
    element: <span class: cls, style: style>,
    height: height,
    depth: depth,
    width: width,
    type: box_type,
    italic: 0.0,
    skew: 0.0
}

// create a box from a text string (leaf node)
pub fn text_box(text, cls, box_type) => {
    element: text_element(text, cls),
    height: text_height_for(text, cls),
    depth: text_depth_for(text, cls),
    height_raw: text_height_raw_for(text, cls),
    depth_raw: text_depth_raw_for(text, cls),
    width: met.DEFAULT_CHAR_WIDTH * float(len(text)),
    type: box_type,
    italic: 0.0,
    skew: 0.0
}

// Full-precision (5dp) height for strut emission. Looks up the metric
// table for both single-char and multi-char text. For +/−/-/ı/ȷ we still
// keep the rounded `height` at 0.69 to avoid cascading into fraction
// constants, but we ALSO expose the truthful 0.58333 here so the outer
// strut can emit the correct h+d sum.
fn text_height_raw_for(text, cls) {
    let normalized = if (text == "-") "−" else text
    if (len(normalized) == 1) {
        let font = font_from_class(cls)
        let m = if (font != null) metrics_data.lookup(normalized, font) else null
        if (m != null) metrics_data.height_raw_of(m) else null
    }
    else if (is_alpha_multi(normalized) and font_from_class(cls) != null)
        max_char_height_raw(normalized, font_from_class(cls))
    else null
}

fn text_depth_raw_for(text, cls) {
    let normalized = if (text == "-") "−" else text
    if (len(normalized) == 1) {
        let font = font_from_class(cls)
        let m = if (font != null) metrics_data.lookup(normalized, font) else null
        if (m != null) metrics_data.depth_raw_of(m) else null
    }
    else if (is_alpha_multi(normalized) and font_from_class(cls) != null)
        max_char_depth_raw(normalized, font_from_class(cls))
    else null
}

fn max_char_height_raw(text, font) {
    max_char_h_raw_loop(text, font, 0, 0.0, true)
}

fn max_char_h_raw_loop(text, font, i, acc, found_any) {
    if (i >= len(text)) {
        if (found_any) acc else null
    } else {
        let m = metrics_data.lookup(text[i], font)
        let h = if (m != null) metrics_data.height_raw_of(m) else null
        if (h == null) null
        else if (h > acc) max_char_h_raw_loop(text, font, i + 1, h, true)
        else max_char_h_raw_loop(text, font, i + 1, acc, true)
    }
}

fn max_char_depth_raw(text, font) {
    max_char_d_raw_loop(text, font, 0, 0.0, true)
}

fn max_char_d_raw_loop(text, font, i, acc, found_any) {
    if (i >= len(text)) {
        if (found_any) acc else null
    } else {
        let m = metrics_data.lookup(text[i], font)
        let d = if (m != null) metrics_data.depth_raw_of(m) else null
        if (d == null) null
        else if (d > acc) max_char_d_raw_loop(text, font, i + 1, d, true)
        else max_char_d_raw_loop(text, font, i + 1, acc, true)
    }
}

// Look up metrics from MathLive's character metrics table when possible,
// falling back to the heuristic text_height/text_depth.
fn font_from_class(cls) {
    if (cls == css.CMR) "cmr"
    else if (cls == css.MATHIT) "mathit"
    else if (cls == css.AMS) "ams"
    else if (cls == "lcGreek lm_mathit") "mathit"
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
    else text_height(text)
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
    // Short-body italic letters (Math-Italic cmmi metrics: height ≈ 0.43056)
    if (text == "x" or text == "o" or text == "m" or text == "n" or
        text == "a" or text == "c" or text == "e" or text == "r" or
        text == "s" or text == "u" or text == "v" or text == "w" or
        text == "z" or
        // Descender short-body letters (height 0.44, depth 0.19 separately)
        text == "g" or text == "y" or text == "p" or text == "q") 0.44
    // Greek lowercase — heights from MathLive Math-Italic probe.
    // Short body (0.44): α γ η ι κ μ ν ο π ρ σ τ υ χ ω ϵ ϕ ϱ
    else if (text == "α" or text == "γ" or text == "η" or text == "ι" or
             text == "κ" or text == "μ" or text == "ν" or text == "ο" or
             text == "π" or text == "ρ" or text == "σ" or text == "τ" or
             text == "υ" or text == "χ" or text == "ω" or
             text == "ε" or text == "φ" or text == "ϱ" or text == "ϕ" or
             text == "ϵ") 0.44
    // Tall body (0.7): β δ ε ζ θ λ ξ φ ψ ϑ
    else if (text == "β" or text == "δ" or text == "ζ" or
             text == "θ" or text == "λ" or text == "ξ" or
             text == "ψ" or text == "ϑ") 0.7
    // Greek uppercase: cmr/Main-Regular height 0.69
    else if (text == "Γ" or text == "Δ" or text == "Θ" or text == "Λ" or
             text == "Π" or text == "Σ" or text == "Υ" or text == "Φ" or
             text == "Ψ" or text == "Ω" or text == "Ξ") 0.69
    // Specific math symbols with non-default heights
    else if (text == "ℏ" or text == "∇") 0.69
    else if (text == "⊂" or text == "⊃" or text == "⊆" or text == "⊇") 0.54
    else if (text == "∪" or text == "∩") 0.56
    else if (text == "△" or text == "▽") 0.55
    // Relation symbols (Main-Regular/cmsy heights vary)
    else if (text == "≈" or text == "≃" or text == "≡") 0.49
    else if (text == "∼") 0.37
    else if (text == "≅") 0.59
    else if (text == ".") 0.11
    else if (text == ",") 0.11
    else if (text == ":") 0.44
    // AMS arrows
    else if (text == "↞" or text == "↠") 0.53
    else if (text == "⇝") 0.38
    else if (text == "⇚" or text == "⇛") 0.64
    // Doubled relations
    else if (text == "⟹" or text == "⟺" or text == "⟸") 0.53
    // Models / bowtie / asymp
    else if (text == "⊨") 0.75
    else if (text == "⋈") 0.51
    else if (text == "≍") 0.47
    else if (text == "≐") 0.67
    // Boxed operators
    else if (text == "⊞" or text == "⊟" or text == "⊠" or text == "⊡") 0.68
    // Negated relations
    else if (text == "≰" or text == "≱") 0.8
    else if (text == "≮" or text == "≯") 0.71
    // Binary operators (±, ∓): cmsy h=0.59
    else if (text == "±" or text == "∓") 0.59
    // Small circles/dots/stars
    else if (text == "∘" or text == "•") 0.45
    else if (text == "⋆" or text == "∗") 0.47
    // Daggers
    else if (text == "†" or text == "‡") 0.7
    // Square set operations
    else if (text == "⊑" or text == "⊒") 0.64
    // Setminus
    else if (text == "∖") 0.75
    // Diamond
    else if (text == "⋄") 0.45
    // Suits (cmsy)
    else if (text == "♡" or text == "♠" or text == "♣" or text == "♢") 0.7
    // Flat / sharp / natural
    else if (text == "♭") 0.75
    else if (text == "♯" or text == "♮") 0.7
    // Re / Im
    else if (text == "ℜ" or text == "ℑ") 0.7
    // Weierstrass p
    else if (text == "℘") 0.44
    // Beth / gimel / daleth
    else if (text == "ℶ" or text == "ℷ" or text == "ℸ") 0.69
    // Tall vertical arrows + nmid: cmsy height 0.75
    else if (text == "↕" or text == "⇕" or text == "∤") 0.75
    else if (text == "+" or text == "−") 0.69
    // Mid-height math operators (cmsy/cmr metrics ≈ 0.55-0.63em)
    else if (text == "⋅" or text == "∗" or text == "⋆" or text == "∘" or
             text == "∙" or text == "⋄") 0.55
    // X-height symbols (cmr/cmsy ≈ 0.44em)
    else if (text == "∞" or text == "∝") 0.44
    // Relations: cmsy M68/M80 metrics ≈ 0.64em
    else if (text == "≤" or text == "≥" or text == "≪" or text == "≫" or
             text == "≺" or text == "≻" or text == "⪯" or text == "⪰" or
             text == "<" or text == ">") 0.64
    // Horizontal arrows: cmr metric M12 = [-0.13, 0.37, ...] — height 0.37
    else if (text == "→" or text == "←" or text == "↔" or
             text == "⇒" or text == "⇐" or text == "⇔" or
             text == "⟶" or text == "⟵" or text == "⟷" or
             text == "⟹" or text == "⟸" or text == "⟺" or
             text == "↦" or text == "⟼" or
             text == "↪" or text == "↩" or
             text == "↗" or text == "↖" or text == "↘" or text == "↙") 0.37
    else if (text == "■" or text == "▲") 0.68
    else if (is_number_text(text)) 0.65
    else if (text_has_tall_delim(text)) 0.75
    // Uppercase Latin letters (cmr Main-Regular height 0.69141 ≈ 0.69)
    else if (len(text) == 1 and text >= "A" and text <= "Z") 0.69
    // Specific lowercase letter heights (cmmi Math-Italic)
    else if (text == "t") 0.62
    else if (text == "i") 0.66
    // Multi-char operator names — cmr metrics for letters that compose them.
    // Probed against MathLive's actual rendering. Without these, the default
    // 0.7 over-estimates strut height for cos/log/sin/etc.
    else if (text == "sin" or text == "sinh" or text == "csc" or
             text == "lim" or text == "liminf" or text == "limsup" or
             text == "min" or text == "max" or text == "deg" or text == "dim") 0.67
    else if (text == "cos" or text == "cosh" or text == "sec" or
             text == "sec" or text == "ker" or text == "hom" or text == "arg") 0.44
    else if (text == "tan" or text == "tanh" or text == "ln") 0.62
    else if (text == "log" or text == "exp" or text == "det" or
             text == "gcd" or text == "lg" or text == "Pr") 0.7
    else met.DEFAULT_CHAR_HEIGHT
}

fn text_depth(text) {
    if (text_has_tall_delim(text)) 0.25
    else if (text == "_") 0.31
    else if (text == "," or text == ";") 0.19
    // For descender letters (g/j/p/q/y/f/Q, plus multi-char strings like
    // "log"/"lim sup"), use cmmi descent ≈ 0.19444.
    else if (text_has_descender(text)) 0.19
    // Up/down arrows: cmsy depth 0.19
    else if (text == "↑" or text == "↓" or text == "⇑" or text == "⇓") 0.19
    // Vertical bi-directional arrows + nmid have deeper descent 0.25
    else if (text == "↕" or text == "⇕" or text == "∤") 0.25
    // Dotless i/j (cmmi M118 depth 0.19)
    else if (text == "ı" or text == "ȷ") 0.19
    // Set membership / perpendicular: cmsy depth ≈ 0.2
    else if (text == "∈" or text == "∋" or text == "∉" or
             text == "⊥" or text == "⊤") 0.2
    // Subset relations: cmsy depth ≈ 0.13
    else if (text == "⊂" or text == "⊃" or text == "⊆" or text == "⊇") 0.13
    // Much-less-than / much-greater-than: cmsy depth ≈ 0.03
    else if (text == "≪" or text == "≫") 0.03
    // Two-head arrows (AMS): depth 0.01
    else if (text == "↞" or text == "↠") 0.01
    // Doubled-line arrows (AMS): depth 0.13
    else if (text == "⇚" or text == "⇛") 0.13
    // Long implies/iff: depth 0.02
    else if (text == "⟹" or text == "⟺" or text == "⟸") 0.02
    // Negated relations: deeper descent
    else if (text == "≰" or text == "≱") 0.3
    else if (text == "≮" or text == "≯") 0.2
    // Models with stem descent
    else if (text == "⊨") 0.24
    // Squiggly-arrow: negative depth
    else if (text == "⇝") 0.0 - 0.14
    else if (text == "≍") 0.0 - 0.04
    else if (text == "≐") 0.0 - 0.14
    // Daggers/wreath: depth 0.19/0.2
    else if (text == "†" or text == "‡" or
             text == "⊓" or text == "⊔" or text == "⨿" or text == "≀" or
             text == "∖") 0.19
    // Pm/Mp: depth 0.08 (matches +)
    else if (text == "±" or text == "∓") 0.08
    // Small symbols with negative depth
    else if (text == "∘" or text == "•") 0.0 - 0.06
    else if (text == "⋆" or text == "∗") 0.0 - 0.04
    // Square set operations
    else if (text == "⊑" or text == "⊒") 0.13
    // Suits and music symbols: depth 0.12 (♡♠♣) or 0.19 (♯♮)
    else if (text == "♡" or text == "♠" or text == "♣" or text == "♢") 0.12
    else if (text == "♯" or text == "♮") 0.19
    // Diamond: negative depth
    else if (text == "⋄") 0.0 - 0.06
    // wp (℘): descender 0.19
    else if (text == "℘") 0.19
    // Greek lowercase descenders (Math-Italic depths ≈ 0.19-0.2).
    else if (text == "β" or text == "γ" or text == "η" or text == "ζ" or
             text == "μ" or text == "ξ" or text == "ρ" or text == "φ" or
             text == "χ" or text == "ψ" or text == "ϕ" or text == "ϱ") 0.19
    // epsilon has slightly deeper descent (0.2)
    else if (text == "ε") 0.2
    // Relations with NEGATIVE depth (sit above baseline). MathLive emits a
    // bottom strut with positive vertical-align for these.
    else if (text == "≈") 0.0 - 0.02
    else if (text == "∼") 0.0 - 0.14
    else if (text == "≃" or text == "≡") 0.0 - 0.04
    else if (text == "≅") 0.0 - 0.03
    // Horizontal arrows have NEGATIVE depth -0.13 in cmr (M12) — they sit
    // entirely above the baseline. The bottom strut accommodates this with
    // a positive vertical-align (height:h+d = 0.37+(-0.13) = 0.24em,
    // vertical-align:-d = 0.13em).
    else if (text == "→" or text == "←" or text == "↔" or
             text == "⇒" or text == "⇐" or text == "⇔" or
             text == "⟶" or text == "⟵" or text == "⟷" or
             text == "⟹" or text == "⟸" or text == "⟺" or
             text == "↦" or text == "⟼" or
             text == "↪" or text == "↩" or
             text == "↗" or text == "↖" or text == "↘" or text == "↙") 0.0 - 0.14
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
    height_raw: 0.0,
    depth_raw: 0.0,
    width: width_em,
    type: "skip",
    italic: 0.0,
    skew: 0.0
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
        skew: 0.0
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
    let suppress_operator_height = has_suppress_hbox_operator_render_height(valid, 0)
    let max_height = if (len(valid) == 0) 0.0
        else max((for (v in valid) v.height))
    let max_depth = if (len(valid) == 0) 0.0
        else max((for (v in valid) hbox_depth_of(v, suppress_text_depth)))
    let max_render_height = if (len(valid) == 0) null
        else max((for (v in valid) hbox_render_height_of(v, suppress_operator_height)))
    let max_render_depth = if (len(valid) == 0) null
        else max((for (v in valid) hbox_render_depth_of(v, suppress_text_depth)))
    let max_render_total = if (len(valid) == 0) null
        else max((for (v in valid) if (v.render_total != null) v.render_total
            else hbox_render_height_of(v, suppress_operator_height) +
                 hbox_render_depth_of(v, suppress_text_depth)))
    let max_left_right_render_depth = if (len(valid) == 0) null
        else max((for (v in valid) if (v.left_right_render_depth != null) v.left_right_render_depth
            else hbox_render_depth_of(v, suppress_text_depth)))
    let max_left_right_render_total = if (len(valid) == 0) null
        else max((for (v in valid) if (v.left_right_render_total != null) v.left_right_render_total
            else if (v.render_total != null) v.render_total
            else hbox_render_height_of(v, suppress_operator_height) +
                 hbox_render_depth_of(v, suppress_text_depth)))
    let strut_depth_em = first_strut_depth_em(valid, 0)
    // Full-precision raw max: propagated for strut emission only. If ANY
    // child lacks a raw value (e.g. composite boxes from fractions/scripts),
    // we conservatively skip propagation and fall back to rounded values
    // at strut time. Initialize with the first child's value so negative
    // raw depths (arrows extending above baseline) propagate correctly.
    let raw_max_h = if (len(valid) == 0) null
        else (let h0 = valid[0].height_raw,
              if (h0 == null) null else max_raw_h(valid, 1, h0))
    let raw_max_d = if (len(valid) == 0) null
        else (let bx0 = valid[0],
              let d0 = if (suppress_text_depth and is_depthless_text_box(bx0)) 0.0
                       else bx0.depth_raw,
              if (d0 == null) null
              else max_raw_d(valid, 1, d0, suppress_text_depth))
    {
        element: <span class: css.BASE;
            for (child in children) child
        >,
        height: max_height,
        depth: max_depth,
        height_raw: raw_max_h,
        depth_raw: raw_max_d,
        render_height: max_render_height,
        render_depth: max_render_depth,
        render_total: max_render_total,
        left_right_render_depth: max_left_right_render_depth,
        left_right_render_total: max_left_right_render_total,
        width: total_width,
        type: "ord",
        italic: 0.0,
        skew: 0.0,
        strut_total: if (len(valid) == 1) valid[0].strut_total else null,
        strut_depth_em: strut_depth_em,
        is_fraction: if (len(valid) == 1) valid[0].is_fraction else null,
        is_script_radical: has_script_radical(valid, 0)
    }
}

// Maximum full-precision height across children. Returns null if any
// child lacks a height_raw entry (so the strut emission falls back to
// the rounded `height` field).
fn max_raw_h(valid, i, acc) {
    if (i >= len(valid)) acc
    else {
        let h = valid[i].height_raw
        if (h == null) null
        else if (h > acc) max_raw_h(valid, i + 1, h)
        else max_raw_h(valid, i + 1, acc)
    }
}

fn max_raw_d(valid, i, acc, suppress_text_depth) {
    if (i >= len(valid)) acc
    else {
        let bx = valid[i]
        let d = if (suppress_text_depth and is_depthless_text_box(bx)) 0.0
                else bx.depth_raw
        if (d == null) null
        else if (d > acc) max_raw_d(valid, i + 1, d, suppress_text_depth)
        else max_raw_d(valid, i + 1, acc, suppress_text_depth)
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

fn first_strut_depth_em(items, i) {
    if (i >= len(items)) null
    else if (items[i].strut_depth_em != null) items[i].strut_depth_em
    else first_strut_depth_em(items, i + 1)
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

fn has_suppress_hbox_operator_render_height(items, i) {
    if (i >= len(items)) false
    else if (items[i].suppress_hbox_operator_render_height == true) true
    else has_suppress_hbox_operator_render_height(items, i + 1)
}

fn hbox_depth_of(bx, suppress_text_depth) {
    if (suppress_text_depth and is_depthless_text_box(bx)) 0.0
    else bx.depth
}

fn hbox_render_height_of(bx, suppress_operator_height) {
    if (suppress_operator_height and is_binary_operator_text_box(bx)) 0.65
    else if (bx.render_height != null) bx.render_height
    else bx.height
}

fn hbox_render_depth_of(bx, suppress_text_depth) {
    if (suppress_text_depth and is_depthless_text_box(bx)) 0.0
    else if (bx.render_depth != null) bx.render_depth
    else bx.depth
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
        skew: 0.0
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
    height_raw: bx.height_raw,
    depth_raw: bx.depth_raw,
    render_height: bx.render_height,
    render_depth: bx.render_depth,
    render_total: bx.render_total,
    left_right_render_depth: bx.left_right_render_depth,
    left_right_render_total: bx.left_right_render_total,
    strut_total: bx.strut_total,
    strut_depth_em: bx.strut_depth_em,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew
}

// wrap a box with inline style string
pub fn with_style(bx, style_str) => {
    element: <span style: style_str; bx.element>,
    height: bx.height,
    depth: bx.depth,
    height_raw: bx.height_raw,
    depth_raw: bx.depth_raw,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew
}

// wrap a box with inline styles (font-size scaling)
pub fn with_scale(bx, scale) {
    if (scale == 1.0) bx
    else
        (let pct = string(round(scale * 1000.0) / 10.0) ++ "%",
         {
            element: <span style: "font-size:" ++ pct; bx.element>,
            height: bx.height * scale,
            depth: bx.depth * scale,
            height_raw: if (bx.height_raw != null) bx.height_raw * scale else null,
            depth_raw: if (bx.depth_raw != null) bx.depth_raw * scale else null,
            width: bx.width * scale,
            type: bx.type,
            italic: bx.italic * scale,
            skew: bx.skew * scale
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
        height_raw: bx.height_raw,
        depth_raw: bx.depth_raw,
        render_height: bx.render_height,
        render_depth: bx.render_depth,
        render_total: bx.render_total,
        width: bx.width,
        type: bx.type,
        italic: bx.italic,
        skew: bx.skew,
        suppress_hbox_text_depth: bx.suppress_hbox_text_depth,
        is_middle_delim: bx.is_middle_delim
    })
}
