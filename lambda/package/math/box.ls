// math/box.ls — Box model for math rendering
// A box is a map: {element, height, depth, width, type, classes, italic, skew}
// Boxes wrap Lambda elements (<span>) and carry metric metadata.

import css: .css
import util: .util
import met: .metrics

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
    height: text_height(text),
    depth: text_depth(text),
    width: met.DEFAULT_CHAR_WIDTH * float(len(text)),
    type: box_type,
    italic: 0.0,
    skew: 0.0
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
    if (cls == "lcGreek lm_mathit" and text == "α") "margin-right:0.01em"
    else if (cls == css.MATHIT and text == "f") "margin-right:0.11em"
    else if (cls == css.MATHIT and text == "g") "margin-right:0.04em"
    else if (cls == css.MATHIT and text == "j") "margin-right:0.06em"
    else if (cls == css.MATHIT and text == "q") "margin-right:0.04em"
    else if (cls == css.MATHIT and text == "y") "margin-right:0.04em"
    else if (cls == css.MATHIT and text == "k") "margin-right:0.04em"
    else if (cls == css.MATHIT and text == "z") "margin-right:0.05em"
    else if (cls == css.CMR and text == "_") "margin-right:0.03em"
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
        text == "z") 0.44
    // Descender letters with short upper body (cmmi height 0.43)
    else if (text == "g" or text == "y") 0.44
    else if (text == "α") 0.44
    else if (text == "Γ" or text == "Δ" or text == "Θ" or text == "Λ" or
             text == "Π" or text == "Σ" or text == "Υ" or text == "Φ" or
             text == "Ψ" or text == "Ω") 0.69
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
    else if (text_has_tall_delim(text)) 0.75 else met.DEFAULT_CHAR_HEIGHT
}

fn text_depth(text) {
    if (text_has_tall_delim(text)) 0.25
    else if (text == "_") 0.31
    else if (text == "," or text == ";") 0.19
    // For descender letters (g/j/p/q/y/f/Q, plus multi-char strings like
    // "log"/"lim sup"), use cmmi descent ≈ 0.19444.
    else if (text_has_descender(text)) 0.19
    // Vertical arrows have descent 0.19 in MathLive's cmsy metrics.
    else if (text == "↑" or text == "↓" or text == "↕" or
             text == "⇑" or text == "⇓" or text == "⇕") 0.19
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
    {
        element: <span class: css.BASE;
            for (child in children) child
        >,
        height: max_height,
        depth: max_depth,
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
