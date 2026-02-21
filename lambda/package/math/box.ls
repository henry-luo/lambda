// math/box.ls — Box model for math rendering
// A box is a map: {element, height, depth, width, type, classes, italic, skew}
// Boxes wrap Lambda elements (<span>) and carry metric metadata.

import css: .lambda.package.math.css
import util: .lambda.package.math.util
import met: .lambda.package.math.metrics

// ============================================================
// Box constructors
// ============================================================

// create a box from an element with specified metrics
pub fn make_box(element, height, depth, width, box_type) => {
    element: element,
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
    element: (if (cls) <span class: cls; text> else <span; text>),
    height: met.DEFAULT_CHAR_HEIGHT,
    depth: met.DEFAULT_CHAR_DEPTH,
    width: met.DEFAULT_CHAR_WIDTH * float(len(text)),
    type: box_type,
    italic: 0.0,
    skew: 0.0
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
    let children = (for (v in valid) v.element)
    let total_width = sum((for (v in valid) v.width))
    let max_height = if (len(valid) == 0) 0.0
        else max((for (v in valid) v.height))
    let max_depth = if (len(valid) == 0) 0.0
        else max((for (v in valid) v.depth))
    {
        element: <span;
            for (child in children) child
        >,
        height: max_height,
        depth: max_depth,
        width: total_width,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// VBox — vertical stacking using inline-table layout
// ============================================================

// create a vertical box with individually shifted children
// children: [{box, shift}] where shift is distance from baseline (negative = up)
// Returns a box wrapping the vlist <span> tree
// helper: build vbox internals when there are children
fn build_vbox(children) {
    let pstrut_size = 2.0 + max((for (c in children) c.box.height + c.box.depth))
    let max_pos = max((for (c in children) 0.0 - c.shift))
    let min_pos = min((for (c in children) 0.0 - c.shift - c.box.height - c.box.depth))
    let items = (for (c in children,
                     let top_val = 0.0 - (pstrut_size + (0.0 - c.shift) + c.box.depth))
        <span style: "top:" ++ util.fmt_em(top_val);
            <span class: css.PSTRUT,
                  style: "height:" ++ util.fmt_em(pstrut_size)>
            c.box.element
        >)
    let extends_below = min_pos < 0.0
    let vlist_el = if (extends_below)
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST,
                      style: "height:" ++ util.fmt_em(max_pos);
                    for (item in items) item
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST,
                      style: "height:" ++ util.fmt_em(0.0 - min_pos);
                    <span>
                >
            >
        >
    else
        <span class: css.VLIST_T;
            <span class: css.VLIST_R;
                <span class: css.VLIST,
                      style: "height:" ++ util.fmt_em(max_pos);
                    for (item in items) item
                >
            >
        >
    {
        element: vlist_el,
        height: max_pos,
        depth: 0.0 - min_pos,
        width: max((for (c in children) c.box.width)),
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
pub fn make_struts(bx) {
    let h = bx.height
    let d = bx.depth
    let strut_bottom_style = "height:" ++ util.fmt_em(h + d) ++ ";vertical-align:" ++ util.fmt_em(0.0 - d)
    <span;
        <span class: css.STRUT,
              style: "height:" ++ util.fmt_em(h)>
        <span class: css.STRUT_BOTTOM,
              style: strut_bottom_style>
        bx.element
    >
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
    else {
        element: <span style: "color:" ++ color; bx.element>,
        height: bx.height,
        depth: bx.depth,
        width: bx.width,
        type: bx.type,
        italic: bx.italic,
        skew: bx.skew
    }
}
