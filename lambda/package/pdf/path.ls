// pdf/path.ls — PDF path-construction & painting operators
//
// Phase 3 scope:
//   - Path construction:    m, l, c, v, y, re, h
//   - Path painting:        S, s, f, F, f*, B, B*, b, b*, n
//   - Line state:           w, J, j, M, d (emitted as SVG stroke attrs)
//
// Path state lives inside the interp.ls graphics state. We do NOT mutate;
// every helper returns a fresh path-state record. The driver (interp.ls)
// drains accumulated <path> elements after each painting operator.
//
// Coordinates are kept in PDF user space. The driver wraps emitted paths
// in the page-level y-flip group so the d="..." strings read naturally
// (PDF +y is up; SVG +y is down).
//
// We follow the same Lambda-script discipline used in text.ls / interp.ls:
//   - No `var x = null` (locks the var to null forever).
//   - No `var s = ""` (locks s as null-string).
//   - Multi-line `++` chains are wrapped in parens.
//   - State manipulations stay inside `fn` (no `pn` here at all).
//
// Path representation: `segments` is a list of records. Each record's
// `cmd` is one of "M", "L", "C", "Z" and carries the absolute coordinates
// already resolved (v/y/re are expanded at construction time).

import util:  .util
import color: .color

// ============================================================
// Operand helper
// ============================================================

// ============================================================
// Initial state
// ============================================================
//
// `start_x`/`start_y` track the start of the current subpath so `h`
// can append a Z. `current_x`/`current_y` track the current point used
// implicitly by v / y.

pub fn new_state() {
    {
        segments:       [],
        current_x:      0.0,
        current_y:      0.0,
        start_x:        0.0,
        start_y:        0.0,
        fill_color:     color.BLACK,
        stroke_color:   color.BLACK,
        fill_opacity:   1.0,
        stroke_opacity: 1.0,
        line_width:     1.0,
        line_cap:       0,
        line_join:      0,
        miter_limit:    10.0,
        dash_array:     [],
        dash_phase:     0.0
    }
}

// ============================================================
// Functional setters
// ============================================================
//
// Every setter rebuilds the whole record by name to side-step Lambda's
// shape-pool quirks with map updates.

fn _set(st, segments, cx, cy, sx, sy) {
    {
        segments:       segments,
        current_x:      cx,
        current_y:      cy,
        start_x:        sx,
        start_y:        sy,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

pub fn set_fill_color(st, c) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     c,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

pub fn set_stroke_color(st, c) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   c,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

// Set fill / stroke alpha (from gs ExtGState ca/CA).
pub fn set_opacity(st, fa, sa) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   fa,
        stroke_opacity: sa,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

fn _set_line_width(st, w) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     w,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

pub fn set_line_width(st, w) { _set_line_width(st, w) }

fn _set_line_cap(st, n) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       n,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

pub fn set_line_cap(st, n) { _set_line_cap(st, n) }

fn _set_line_join(st, n) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      n,
        miter_limit:    st.miter_limit,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

pub fn set_line_join(st, n) { _set_line_join(st, n) }

fn _set_miter(st, m) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    m,
        dash_array:     st.dash_array,
        dash_phase:     st.dash_phase
    }
}

pub fn set_miter_limit(st, m) { _set_miter(st, m) }

fn _set_dash(st, arr, phase) {
    {
        segments:       st.segments,
        current_x:      st.current_x,
        current_y:      st.current_y,
        start_x:        st.start_x,
        start_y:        st.start_y,
        fill_color:     st.fill_color,
        stroke_color:   st.stroke_color,
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
        line_width:     st.line_width,
        line_cap:       st.line_cap,
        line_join:      st.line_join,
        miter_limit:    st.miter_limit,
        dash_array:     arr,
        dash_phase:     phase
    }
}

pub fn set_dash(st, arr, phase) { _set_dash(st, arr, phase) }

// ============================================================
// Path construction operators
// ============================================================

fn _op_m(st, ops) {
    if (len(ops) >= 2) {
        let x = util.num(ops[0])
        let y = util.num(ops[1])
        let segs = st.segments ++ [{ cmd: "M", x: x, y: y }]
        _set(st, segs, x, y, x, y)
    }
    else { st }
}

fn _op_l(st, ops) {
    if (len(ops) >= 2) {
        let x = util.num(ops[0])
        let y = util.num(ops[1])
        let segs = st.segments ++ [{ cmd: "L", x: x, y: y }]
        _set(st, segs, x, y, st.start_x, st.start_y)
    }
    else { st }
}

fn _op_c(st, ops) {
    if (len(ops) >= 6) {
        let x1 = util.num(ops[0]); let y1 = util.num(ops[1])
        let x2 = util.num(ops[2]); let y2 = util.num(ops[3])
        let x3 = util.num(ops[4]); let y3 = util.num(ops[5])
        let segs = st.segments ++ [{ cmd: "C",
                                     x1: x1, y1: y1,
                                     x2: x2, y2: y2,
                                     x3: x3, y3: y3 }]
        _set(st, segs, x3, y3, st.start_x, st.start_y)
    }
    else { st }
}

// v: first control point = current point; produces a C with cur,cur,x2,y2,x3,y3
fn _op_v(st, ops) {
    if (len(ops) >= 4) {
        let x2 = util.num(ops[0]); let y2 = util.num(ops[1])
        let x3 = util.num(ops[2]); let y3 = util.num(ops[3])
        let segs = st.segments ++ [{ cmd: "C",
                                     x1: st.current_x, y1: st.current_y,
                                     x2: x2, y2: y2,
                                     x3: x3, y3: y3 }]
        _set(st, segs, x3, y3, st.start_x, st.start_y)
    }
    else { st }
}

// y: second control point = endpoint; C with x1,y1,x3,y3,x3,y3
fn _op_y(st, ops) {
    if (len(ops) >= 4) {
        let x1 = util.num(ops[0]); let y1 = util.num(ops[1])
        let x3 = util.num(ops[2]); let y3 = util.num(ops[3])
        let segs = st.segments ++ [{ cmd: "C",
                                     x1: x1, y1: y1,
                                     x2: x3, y2: y3,
                                     x3: x3, y3: y3 }]
        _set(st, segs, x3, y3, st.start_x, st.start_y)
    }
    else { st }
}

// re x y w h: append a closed rectangle subpath.
fn _op_re(st, ops) {
    if (len(ops) >= 4) {
        let x = util.num(ops[0])
        let y = util.num(ops[1])
        let w = util.num(ops[2])
        let h = util.num(ops[3])
        let segs = (st.segments
                    ++ [{ cmd: "M", x: x,     y: y     }]
                    ++ [{ cmd: "L", x: x + w, y: y     }]
                    ++ [{ cmd: "L", x: x + w, y: y + h }]
                    ++ [{ cmd: "L", x: x,     y: y + h }]
                    ++ [{ cmd: "Z" }])
        _set(st, segs, x, y, x, y)
    }
    else { st }
}

fn _op_h(st) {
    let segs = st.segments ++ [{ cmd: "Z" }]
    _set(st, segs, st.start_x, st.start_y, st.start_x, st.start_y)
}

// ============================================================
// d-string serialization
// ============================================================

fn _seg_str(s) {
    if (s.cmd == "M") {
        "M " ++ util.fmt_num(s.x) ++ " " ++ util.fmt_num(s.y)
    }
    else if (s.cmd == "L") {
        "L " ++ util.fmt_num(s.x) ++ " " ++ util.fmt_num(s.y)
    }
    else if (s.cmd == "C") {
        ("C " ++ util.fmt_num(s.x1) ++ " " ++ util.fmt_num(s.y1) ++ " "
              ++ util.fmt_num(s.x2) ++ " " ++ util.fmt_num(s.y2) ++ " "
              ++ util.fmt_num(s.x3) ++ " " ++ util.fmt_num(s.y3))
    }
    else if (s.cmd == "Z") { "Z" }
    else                   { "" }
}

fn _segments_to_d(segments) {
    let parts = (for (s in segments) _seg_str(s))
    parts | join(" ")
}

// Public: render the *current* path's segments as an SVG `d` string.
// Used by interp.ls to capture the clipping path on W / W* before the
// next painting operator clears the segments.
pub fn current_path_d(st) {
    if (st == null or len(st.segments) == 0) { "" }
    else { _segments_to_d(st.segments) }
}

// Public: true when the current state has any path segments queued.
pub fn has_segments(st) {
    (st != null) and (len(st.segments) > 0)
}

// ============================================================
// Painting
// ============================================================
//
// Each painting operator returns a record { state, emit } where:
//   - state is the path state with `segments` cleared
//   - emit  is a list of <path> SVG elements (length 0 or 1)

fn _clear(st) {
    _set(st, [], st.current_x, st.current_y, st.start_x, st.start_y)
}

pub fn clear_current_path(st) {
    _set(st, [], st.current_x, st.current_y, st.start_x, st.start_y)
}

fn _close_last(st) {
    let segs = st.segments ++ [{ cmd: "Z" }]
    _set(st, segs, st.start_x, st.start_y, st.start_x, st.start_y)
}

// Format dash array as SVG `stroke-dasharray` value. Empty PDF array =
// solid stroke ("none" in SVG).
fn _format_dash(arr) {
    if (arr == null) { "none" }
    else if (len(arr) == 0) { "none" }
    else {
        let parts = (for (n in arr) util.fmt_num(n))
        parts | join(",")
    }
}

fn _line_cap_name(n) {
    if (n == 1) { "round" }
    else if (n == 2) { "square" }
    else { "butt" }
}

fn _line_join_name(n) {
    if (n == 1) { "round" }
    else if (n == 2) { "bevel" }
    else { "miter" }
}

fn _emit_path(st, fill, stroke, fill_rule) {
    if (len(st.segments) == 0) { { state: _clear(st), emit: [] } }
    else {
        let d = _segments_to_d(st.segments)
        let lw = st.line_width
        let dash = _format_dash(st.dash_array)
        let cap = _line_cap_name(st.line_cap)
        let join = _line_join_name(st.line_join)
        let miter = util.fmt_num(st.miter_limit)
        let has_extras = ((dash != "none")
                          or (st.fill_opacity < 1.0)
                          or (st.stroke_opacity < 1.0))
        let elem = if (has_extras and (fill_rule == "evenodd")) {
            <path d: d,
                  fill: fill,
                  'fill-rule': "evenodd",
                  'fill-opacity':   util.fmt_num(st.fill_opacity),
                  stroke: stroke,
                  'stroke-opacity': util.fmt_num(st.stroke_opacity),
                  'stroke-width':   util.fmt_num(lw),
                  'stroke-linecap': cap,
                  'stroke-linejoin': join,
                  'stroke-miterlimit': miter,
                  'stroke-dasharray': dash,
                  'stroke-dashoffset': util.fmt_num(st.dash_phase)>
        }
        else if (has_extras) {
            <path d: d,
                  fill: fill,
                  'fill-opacity':   util.fmt_num(st.fill_opacity),
                  stroke: stroke,
                  'stroke-opacity': util.fmt_num(st.stroke_opacity),
                  'stroke-width':   util.fmt_num(lw),
                  'stroke-linecap': cap,
                  'stroke-linejoin': join,
                  'stroke-miterlimit': miter,
                  'stroke-dasharray': dash,
                  'stroke-dashoffset': util.fmt_num(st.dash_phase)>
        }
        else if (fill_rule == "evenodd") {
            <path d: d,
                  fill: fill,
                  'fill-rule': "evenodd",
                  stroke: stroke,
                  'stroke-linecap': cap,
                  'stroke-linejoin': join,
                  'stroke-miterlimit': miter,
                  'stroke-width': util.fmt_num(lw)>
        }
        else {
            <path d: d,
                  fill: fill,
                  stroke: stroke,
                  'stroke-linecap': cap,
                  'stroke-linejoin': join,
                  'stroke-miterlimit': miter,
                  'stroke-width': util.fmt_num(lw)>
        }
        { state: _clear(st), emit: [elem] }
    }
}

// S: stroke (no close)
fn _op_S(st)  { _emit_path(st, "none", st.stroke_color, "nonzero") }
// s: close + stroke
fn _op_s(st)  { _emit_path(_close_last(st), "none", st.stroke_color, "nonzero") }
// f / F: fill nonzero
fn _op_f(st)  { _emit_path(st, st.fill_color, "none", "nonzero") }
// f*: fill evenodd
fn _op_fstar(st) { _emit_path(st, st.fill_color, "none", "evenodd") }
// B: fill nonzero + stroke
fn _op_B(st)  { _emit_path(st, st.fill_color, st.stroke_color, "nonzero") }
// B*: fill evenodd + stroke
fn _op_Bstar(st) { _emit_path(st, st.fill_color, st.stroke_color, "evenodd") }
// b: close + fill nonzero + stroke
fn _op_b(st)  { _emit_path(_close_last(st), st.fill_color, st.stroke_color, "nonzero") }
// b*: close + fill evenodd + stroke
fn _op_bstar(st) { _emit_path(_close_last(st), st.fill_color, st.stroke_color, "evenodd") }
// n: end path with no paint, just clear
fn _op_n(st)  { { state: _clear(st), emit: [] } }

// ============================================================
// Line-state operators
// ============================================================

fn _op_w(st, ops) {
    if (len(ops) >= 1) { _set_line_width(st, util.num(ops[0])) } else { st }
}

fn _op_J(st, ops) {
    if (len(ops) >= 1) { _set_line_cap(st, util.int_or(ops[0], 0)) } else { st }
}

fn _op_j(st, ops) {
    if (len(ops) >= 1) { _set_line_join(st, util.int_or(ops[0], 0)) } else { st }
}

fn _op_M(st, ops) {
    if (len(ops) >= 1) { _set_miter(st, util.num(ops[0])) } else { st }
}

// d [ array ] phase
fn _op_d(st, ops) {
    if (len(ops) >= 2 and ops[0] is map and ops[0].kind == "array") {
        let raw = ops[0].value
        let nums = (for (n in raw) util.num(n))
        _set_dash(st, nums, util.num(ops[1]))
    }
    else { st }
}

// ============================================================
// Top-level dispatcher
// ============================================================
//
// Returns { state, emit } where emit is a (possibly empty) list of SVG
// path elements.  Construction-only ops emit []; painting ops emit a
// (possibly empty) singleton; unknown ops are returned as a no-op so
// the interp.ls catch-all keeps working.

pub fn apply_op(st, opr, ops) {
    if (opr == "m") { { state: _op_m(st, ops),  emit: [] } }
    else if (opr == "l") { { state: _op_l(st, ops),  emit: [] } }
    else if (opr == "c") { { state: _op_c(st, ops),  emit: [] } }
    else if (opr == "v") { { state: _op_v(st, ops),  emit: [] } }
    else if (opr == "y") { { state: _op_y(st, ops),  emit: [] } }
    else if (opr == "re"){ { state: _op_re(st, ops), emit: [] } }
    else if (opr == "h") { { state: _op_h(st),       emit: [] } }
    else if (opr == "S") { _op_S(st) }
    else if (opr == "s") { _op_s(st) }
    else if (opr == "f") { _op_f(st) }
    else if (opr == "F") { _op_f(st) }
    else if (opr == "f*"){ _op_fstar(st) }
    else if (opr == "B") { _op_B(st) }
    else if (opr == "B*"){ _op_Bstar(st) }
    else if (opr == "b") { _op_b(st) }
    else if (opr == "b*"){ _op_bstar(st) }
    else if (opr == "n") { _op_n(st) }
    else if (opr == "w") { { state: _op_w(st, ops), emit: [] } }
    else if (opr == "J") { { state: _op_J(st, ops), emit: [] } }
    else if (opr == "j") { { state: _op_j(st, ops), emit: [] } }
    else if (opr == "M") { { state: _op_M(st, ops), emit: [] } }
    else if (opr == "d") { { state: _op_d(st, ops), emit: [] } }
    else                 { { state: st, emit: [] } }
}

// Predicate so interp.ls can quickly route ops without a giant `or`.
pub fn handles(opr) {
    ((opr == "m") or (opr == "l") or (opr == "c") or (opr == "v") or (opr == "y")
        or (opr == "re") or (opr == "h")
        or (opr == "S") or (opr == "s")
        or (opr == "f") or (opr == "F") or (opr == "f*")
        or (opr == "B") or (opr == "B*")
        or (opr == "b") or (opr == "b*")
        or (opr == "n")
        or (opr == "w") or (opr == "J") or (opr == "j") or (opr == "M") or (opr == "d"))
}
