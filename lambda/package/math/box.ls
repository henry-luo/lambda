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
