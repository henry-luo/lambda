// latex/elements/picture.ls — Picture environment → SVG rendering
// Converts \begin{picture}(w,h) ... \end{picture} to inline SVG
//
// AST structure: <picture <paragraph "(w,h)" ...commands... >>
// Commands are flat siblings: <put> "(x,y)" <curly_group body>
// LaTeX picture Y-axis is bottom-up; SVG is top-down — we flip Y coords.
//
// State is passed as 3 individual floats: sw (stroke-width), sc (scale), ht (height)
// to avoid map type inference issues in the transpiler.
//
// Coordinates are returned as ["x_str", "y_str"] string arrays to avoid
// the transpiler inferring [float, float] as a map type.

// ============================================================
// Coordinate parsing — returns ["x", "y"] string arrays
// ============================================================

// parse "(x,y)" → ["x_str", "y_str"] or null
pub fn parse_coord(text) {
    if (text == null) { null }
    else {
        let t = trim(text)
        let lp = index_of(t, "(")
        let rp = index_of(t, ")")
        if (lp < 0 or rp < 0) { null }
        else {
            let inner = slice(t, lp + 1, rp)
            let parts = split(inner, ",")
            if (len(parts) < 2) { null }
            else { [trim(parts[0]), trim(parts[1])] }
        }
    }
}

// get float from coord string pair
fn coord_x(coord) { parse_num(coord[0]) }
fn coord_y(coord) { parse_num(coord[1]) }

// helpers to avoid transpiler map-inference on if-expr
fn pic_dim(wh, idx, def) {
    if (wh == null) { def }
    else { parse_num(wh[idx]) }
}
fn safe_int(v) {
    if (v == null) { 0 }
    else { v }
}
fn next_curly_or(para, idx, n, def) {
    if (idx < n) { find_next_curly(para, idx, n) }
    else { def }
}
fn strip_unit(t) {
    if (ends_with(t, "mm")) { slice(t, 0, len(t) - 2) }
    else if (ends_with(t, "pt")) { slice(t, 0, len(t) - 2) }
    else if (ends_with(t, "cm")) { slice(t, 0, len(t) - 2) }
    else { t }
}

// parse "(x1,y1)(x2,y2)" → [[x,y], [x,y]]
fn parse_two_coords(text) {
    if (text == null) { [] }
    else {
        let t = trim(text)
        let first_rp = index_of(t, ")")
        if (first_rp < 0) { [] }
        else {
            let c1 = parse_coord(slice(t, 0, first_rp + 1))
            let rest = slice(t, first_rp + 1, len(t))
            let c2 = parse_coord(rest)
            if (c1 == null) { [] }
            else if (c2 == null) { [c1] }
            else { [c1, c2] }
        }
    }
}

// parse "(x1,y1)(x2,y2)(x3,y3)" → list of [x,y] arrays
fn parse_three_coords(text) {
    if (text == null) { [] }
    else { extract_all_coords(trim(text), 0, []) }
}

fn extract_all_coords(text, start, acc) {
    if (start >= len(text)) { acc }
    else {
        let lp = index_of(slice(text, start, len(text)), "(")
        if (lp < 0) { acc }
        else {
            let abs_lp = start + lp
            let rp = index_of(slice(text, abs_lp, len(text)), ")")
            if (rp < 0) { acc }
            else {
                let abs_rp = abs_lp + rp
                let c = parse_coord(slice(text, abs_lp, abs_rp + 1))
                if (c != null) { extract_all_coords(text, abs_rp + 1, acc ++ [c]) }
                else { extract_all_coords(text, abs_rp + 1, acc) }
            }
        }
    }
}

fn parse_num(s) {
    if (s == null or s == "") { 0.0 }
    else {
        let v = float(s)
        if (v != null) { v }
        else {
            let iv = int(s)
            if (iv != null) { float(iv) }
            else { 0.0 }
        }
    }
}

// ============================================================
// SVG generation — main entry point
// ============================================================

pub fn render_picture(el, unitlength_str = null) {
    if (len(el) > 0 and el[0] is element) { render_picture_para(el[0], unitlength_str) }
    else { render_picture_para(el, unitlength_str) }
}

fn render_picture_para(para, unitlength_str) {
    let n = len(para)
    if (n == 0) {
        <svg class: "latex-picture">
    } else {
        let size_text = find_first_text(para, 0, n)
        let wh = parse_coord(size_text)
        let w = pic_dim(wh, 0, 100.0)
        let h = pic_dim(wh, 1, 100.0)
        let sc = unitlength_to_scale(unitlength_str)
        let svg_w = w * sc
        let svg_h = h * sc
        let vb = "0 0 " ++ fmt(svg_w) ++ " " ++ fmt(svg_h)

        let start_idx = find_first_command_idx(para, 0, n)
        let svg_children = process_commands(para, start_idx, n, 0.4, sc, h, [])

        <svg class: "latex-picture",
             xmlns: "http://www.w3.org/2000/svg",
             viewBox: vb,
             width: fmt(svg_w) ++ "px",
             height: fmt(svg_h) ++ "px",
             style: "overflow:visible";
            <defs;
                <marker id: "arrowhead",
                        markerWidth: "10", markerHeight: "7",
                        refX: "10", refY: "3.5",
                        orient: "auto";
                    <polygon points: "0 0, 10 3.5, 0 7", fill: "black">
                >
            >
            for child in svg_children { child }
        >
    }
}

fn get_paragraph(el) {
    if (len(el) > 0 and el[0] is element) { el[0] }
    else { el }
}

fn find_first_text(el, i, n) {
    if (i >= n) { "" }
    else {
        let child = el[i]
        if (child is string and contains(trim(child), "(")) { trim(child) }
        else { find_first_text(el, i + 1, n) }
    }
}

fn find_first_command_idx(el, i, n) {
    if (i >= n) { n }
    else {
        let child = el[i]
        if (child is element) {
            let tag = string(name(child))
            if (tag == "put" or tag == "multiput" or tag == "qbezier" or
                tag == "thicklines" or tag == "thinlines" or tag == "linethickness") { i }
            else { find_first_command_idx(el, i + 1, n) }
        } else { find_first_command_idx(el, i + 1, n) }
    }
}

// ============================================================
// Command processing — walk flat sibling list
// sw=stroke_width, sc=scale, ht=picture_height
// ============================================================

fn process_commands(para, i, n, sw, sc, ht, acc) {
    if (i >= n) { acc }
    else {
        let child = para[i]
        if (child is element) {
            let tag = string(name(child))
            if (tag == "put") {
                let coord_text = get_next_text(para, i + 1, n)
                let coord = parse_coord(coord_text)
                let body_idx = find_next_curly(para, i + 1, n)
                if (coord != null and body_idx < n) {
                    let body = para[body_idx]
                    let svg_els = render_put_body(body, 0, len(body), coord_x(coord), coord_y(coord), sw, sc, ht, [])
                    process_commands(para, body_idx + 1, n, sw, sc, ht, acc ++ svg_els)
                } else { process_commands(para, i + 1, n, sw, sc, ht, acc) }
            }
            else if (tag == "multiput") {
                let coord_text = get_next_text(para, i + 1, n)
                let coords = parse_two_coords(coord_text)
                let first_curly = find_next_curly(para, i + 1, n)
                let second_curly = next_curly_or(para, first_curly + 1, n, n)
                if (len(coords) >= 2 and second_curly < n) {
                    let sx0 = coord_x(coords[0])
                    let sy0 = coord_y(coords[0])
                    let dx = coord_x(coords[1])
                    let dy = coord_y(coords[1])
                    let count_text = trim(text_of_curly(para[first_curly]))
                    let count_val = int(count_text)
                    let count = safe_int(count_val)
                    let body = para[second_curly]
                    let svg_els = build_multiput(sx0, sy0, dx, dy, count, body, sw, sc, ht, 0, [])
                    process_commands(para, second_curly + 1, n, sw, sc, ht, acc ++ svg_els)
                } else { process_commands(para, i + 1, n, sw, sc, ht, acc) }
            }
            else if (tag == "qbezier") {
                let coord_text = get_next_text(para, i + 1, n)
                let coords = parse_three_coords(coord_text)
                if (len(coords) >= 3) {
                    let svg_el = make_qbezier(
                        coord_x(coords[0]), coord_y(coords[0]),
                        coord_x(coords[1]), coord_y(coords[1]),
                        coord_x(coords[2]), coord_y(coords[2]),
                        sw, sc, ht)
                    process_commands(para, i + 2, n, sw, sc, ht, acc ++ [svg_el])
                } else { process_commands(para, i + 1, n, sw, sc, ht, acc) }
            }
            else if (tag == "thicklines") {
                process_commands(para, i + 1, n, 0.8, sc, ht, acc)
            }
            else if (tag == "thinlines") {
                process_commands(para, i + 1, n, 0.4, sc, ht, acc)
            }
            else if (tag == "linethickness") {
                let thick = parse_length(trim(text_of_curly(child)))
                process_commands(para, i + 1, n, thick, sc, ht, acc)
            }
            else { process_commands(para, i + 1, n, sw, sc, ht, acc) }
        } else { process_commands(para, i + 1, n, sw, sc, ht, acc) }
    }
}

// ============================================================
// Helpers for text/curly_group sibling traversal
// ============================================================

fn get_next_text(para, i, n) {
    if (i >= n) { "" }
    else {
        let child = para[i]
        if (child is string) {
            let t = trim(child)
            if (contains(t, "(")) { t }
            else { get_next_text(para, i + 1, n) }
        } else { "" }
    }
}

fn find_next_curly(para, i, n) {
    if (i >= n) { n }
    else {
        let child = para[i]
        if (child is element and string(name(child)) == "curly_group") { i }
        else { find_next_curly(para, i + 1, n) }
    }
}

fn text_of_curly(el) {
    if (el is string) { el }
    else if (el is element) { join_text(el, 0, len(el), "") }
    else { "" }
}

fn join_text(el, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        if (child is string) { join_text(el, i + 1, n, acc ++ child) }
        else if (child is element) { join_text(el, i + 1, n, acc ++ text_of_curly(child)) }
        else { join_text(el, i + 1, n, acc) }
    }
}

fn find_brack_text(body, i, n) {
    if (i >= n) { null }
    else {
        let child = body[i]
        if (child is string) {
            let t = trim(child)
            let bp = index_of(t, "[")
            if (bp >= 0) {
                let ep = index_of(t, "]")
                if (ep > bp) { trim(slice(t, bp + 1, ep)) }
                else { null }
            } else { null }
        } else { null }
    }
}

// ============================================================
// Rendering \put body — walk children of curly_group
// px,py = position, sw/sc/ht = state
// ============================================================

fn render_put_body(body, i, n, px, py, sw, sc, ht, acc) {
    if (i >= n) { acc }
    else {
        let child = body[i]
        if (child is element) {
            let tag = string(name(child))
            if (tag == "line") {
                let dir_text = get_next_text(body, i + 1, n)
                let dir = parse_coord(dir_text)
                let len_idx = find_next_curly(body, i + 1, n)
                if (dir != null and len_idx < n) {
                    let length = parse_num(trim(text_of_curly(body[len_idx])))
                    let svg_el = make_line(px, py, coord_x(dir), coord_y(dir), length, sw, sc, ht)
                    render_put_body(body, len_idx + 1, n, px, py, sw, sc, ht, acc ++ [svg_el])
                } else { render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc) }
            }
            else if (tag == "vector") {
                let dir_text = get_next_text(body, i + 1, n)
                let dir = parse_coord(dir_text)
                let len_idx = find_next_curly(body, i + 1, n)
                if (dir != null and len_idx < n) {
                    let length = parse_num(trim(text_of_curly(body[len_idx])))
                    let svg_el = make_vector(px, py, coord_x(dir), coord_y(dir), length, sw, sc, ht)
                    render_put_body(body, len_idx + 1, n, px, py, sw, sc, ht, acc ++ [svg_el])
                } else { render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc) }
            }
            else if (tag == "circle") {
                let diam = parse_num(trim(text_of_curly(child)))
                let svg_el = make_circle(px, py, diam, false, sw, sc, ht)
                render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc ++ [svg_el])
            }
            else if (tag == "circle*") {
                let diam = parse_num(trim(text_of_curly(child)))
                let svg_el = make_circle(px, py, diam, true, sw, sc, ht)
                render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc ++ [svg_el])
            }
            else if (tag == "oval") {
                let size_text = get_next_text(body, i + 1, n)
                let size = parse_coord(size_text)
                let part = find_brack_text(body, i + 1, n)
                if (size != null) {
                    let svg_el = make_oval(px, py, coord_x(size), coord_y(size), part, sw, sc, ht)
                    render_put_body(body, i + 2, n, px, py, sw, sc, ht, acc ++ [svg_el])
                } else { render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc) }
            }
            else if (tag == "inline_math" or tag == "display_math") {
                let math_text = text_of_curly(child)
                let svg_el = make_text(px, py, "$" ++ math_text ++ "$", sc, ht)
                render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc ++ [svg_el])
            }
            else { render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc) }
        }
        else if (child is string) {
            let t = trim(child)
            if (t != "" and not contains(t, "(") and not contains(t, "[")) {
                let svg_el = make_text(px, py, t, sc, ht)
                render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc ++ [svg_el])
            } else { render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc) }
        }
        else { render_put_body(body, i + 1, n, px, py, sw, sc, ht, acc) }
    }
}

// ============================================================
// \multiput — iterative expansion
// ============================================================

fn build_multiput(px, py, dx, dy, count, body, sw, sc, ht, i, acc) {
    if (i >= count) { acc }
    else {
        let svg_els = render_put_body(body, 0, len(body), px, py, sw, sc, ht, [])
        build_multiput(px + dx, py + dy, dx, dy, count, body, sw, sc, ht, i + 1, acc ++ svg_els)
    }
}

// ============================================================
// SVG element builders — all take individual float params
// ============================================================

fn flip_y(y, ht) { ht - y }
fn cx(x, sc) { x * sc }
fn cy(y, ht, sc) { flip_y(y, ht) * sc }

fn make_line(px, py, dx, dy, length, sw, sc, ht) {
    let x2 = px + line_dx(dx, length)
    let y2 = py + line_dy(dx, dy, length)
    let sty = "stroke:black;stroke-width:" ++ fmt(sw)
    <line x1: fmt(cx(px, sc)), y1: fmt(cy(py, ht, sc)),
          x2: fmt(cx(x2, sc)), y2: fmt(cy(y2, ht, sc)),
          style: sty>
}

fn make_vector(px, py, dx, dy, length, sw, sc, ht) {
    let x2 = px + line_dx(dx, length)
    let y2 = py + line_dy(dx, dy, length)
    let sty = "stroke:black;stroke-width:" ++ fmt(sw) ++ ";marker-end:url(#arrowhead)"
    <line x1: fmt(cx(px, sc)), y1: fmt(cy(py, ht, sc)),
          x2: fmt(cx(x2, sc)), y2: fmt(cy(y2, ht, sc)),
          style: sty>
}

fn line_dx(dx, length) {
    if (dx != 0.0) { length }
    else { 0.0 }
}

fn line_dy(dx, dy, length) {
    if (dx != 0.0) { length * dy / dx }
    else { line_dy_vert(dy, length) }
}

fn line_dy_vert(dy, length) {
    if (dy > 0.0) { length }
    else { 0.0 - length }
}

fn make_circle(px, py, diam, filled, sw, sc, ht) {
    let r = diam * sc / 2.0
    if (filled) {
        <circle cx: fmt(cx(px, sc)), cy: fmt(cy(py, ht, sc)), r: fmt(r), fill: "black">
    } else {
        let sty = "fill:none;stroke:black;stroke-width:" ++ fmt(sw)
        <circle cx: fmt(cx(px, sc)), cy: fmt(cy(py, ht, sc)), r: fmt(r), style: sty>
    }
}

fn make_oval(px, py, ow, oh, part, sw, sc, ht) {
    let ocx = cx(px, sc)
    let ocy = cy(py, ht, sc)
    let rx = ow * sc / 2.0
    let ry = oh * sc / 2.0
    let sty = "fill:none;stroke:black;stroke-width:" ++ fmt(sw)
    if (part == null) {
        <ellipse cx: fmt(ocx), cy: fmt(ocy), rx: fmt(rx), ry: fmt(ry), style: sty>
    } else {
        let d = build_oval_arc(ocx, ocy, rx, ry, part)
        <path d: d, style: sty>
    }
}

fn build_oval_arc(ocx, ocy, rx, ry, part) {
    if (part == "t") {
        "M " ++ fmt(ocx - rx) ++ " " ++ fmt(ocy) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 1 " ++ fmt(ocx + rx) ++ " " ++ fmt(ocy)
    }
    else if (part == "b") {
        "M " ++ fmt(ocx - rx) ++ " " ++ fmt(ocy) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 0 " ++ fmt(ocx + rx) ++ " " ++ fmt(ocy)
    }
    else if (part == "l") {
        "M " ++ fmt(ocx) ++ " " ++ fmt(ocy - ry) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 0 " ++ fmt(ocx) ++ " " ++ fmt(ocy + ry)
    }
    else if (part == "r") {
        "M " ++ fmt(ocx) ++ " " ++ fmt(ocy - ry) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 1 " ++ fmt(ocx) ++ " " ++ fmt(ocy + ry)
    }
    else if (part == "tl") {
        "M " ++ fmt(ocx) ++ " " ++ fmt(ocy - ry) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 0 " ++ fmt(ocx - rx) ++ " " ++ fmt(ocy)
    }
    else if (part == "tr") {
        "M " ++ fmt(ocx + rx) ++ " " ++ fmt(ocy) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 0 " ++ fmt(ocx) ++ " " ++ fmt(ocy - ry)
    }
    else if (part == "bl") {
        "M " ++ fmt(ocx - rx) ++ " " ++ fmt(ocy) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 0 " ++ fmt(ocx) ++ " " ++ fmt(ocy + ry)
    }
    else if (part == "br") {
        "M " ++ fmt(ocx) ++ " " ++ fmt(ocy + ry) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 0 0 " ++ fmt(ocx + rx) ++ " " ++ fmt(ocy)
    }
    else {
        "M " ++ fmt(ocx - rx) ++ " " ++ fmt(ocy) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 1 1 " ++ fmt(ocx + rx) ++ " " ++ fmt(ocy) ++ " A " ++ fmt(rx) ++ " " ++ fmt(ry) ++ " 0 1 1 " ++ fmt(ocx - rx) ++ " " ++ fmt(ocy)
    }
}

fn make_qbezier(x1, y1, x2, y2, x3, y3, sw, sc, ht) {
    let d = "M " ++ fmt(cx(x1, sc)) ++ " " ++ fmt(cy(y1, ht, sc)) ++
            " Q " ++ fmt(cx(x2, sc)) ++ " " ++ fmt(cy(y2, ht, sc)) ++
            " " ++ fmt(cx(x3, sc)) ++ " " ++ fmt(cy(y3, ht, sc))
    let sty = "fill:none;stroke:black;stroke-width:" ++ fmt(sw)
    <path d: d, style: sty>
}

fn make_text(px, py, text, sc, ht) {
    let sty = "font-size:" ++ fmt(sc) ++ "px;fill:black"
    <text x: fmt(cx(px, sc)), y: fmt(cy(py, ht, sc)), style: sty;
        text
    >
}

// ============================================================
// Formatting helpers
// ============================================================

fn fmt(num) {
    let rounded = float(int(num * 100.0)) / 100.0
    if (rounded == float(int(rounded))) { string(int(rounded)) }
    else { string(rounded) }
}

// Convert \unitlength string (e.g. "20.4mm", "5cm", "1in") to SVG scale factor.
// Scale = unitlength_in_mm * 10.0 (10 SVG pixels per millimeter).
// Default: 10.0 (assumes unitlength = 1mm).
fn unitlength_to_scale(ul_str) {
    if (ul_str == null or ul_str == "") { 10.0 }
    else {
        let t = trim(ul_str)
        if (ends_with(t, "mm")) {
            let v = float(trim(slice(t, 0, len(t) - 2)))
            if (v != null) { v * 10.0 } else { 10.0 }
        }
        else if (ends_with(t, "cm")) {
            let v = float(trim(slice(t, 0, len(t) - 2)))
            if (v != null) { v * 100.0 } else { 10.0 }
        }
        else if (ends_with(t, "in")) {
            let v = float(trim(slice(t, 0, len(t) - 2)))
            if (v != null) { v * 254.0 } else { 10.0 }
        }
        else if (ends_with(t, "pt")) {
            let v = float(trim(slice(t, 0, len(t) - 2)))
            if (v != null) { v * 3.528 } else { 10.0 }
        }
        else {
            let v = float(t)
            if (v != null) { v * 10.0 } else { 10.0 }
        }
    }
}

fn parse_length(text) {
    parse_length_inner(text, 0.4)
}

fn parse_length_inner(text, def) {
    if (text == null) { def }
    else {
        let t = trim(text)
        let t2 = strip_unit(t)
        let v = float(trim(t2))
        float_or(v, def)
    }
}

fn float_or(v, def) {
    if (v != null) { v }
    else { def }
}
