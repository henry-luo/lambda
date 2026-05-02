// pdf/interp.ls — page-level content-stream interpreter
//
// Phase 2 scope:
//   - Drive the operator stream from stream.parse_content_stream.
//   - Pre-resolve fonts referenced by Tf via font.resolve_font so that
//     text.set_font_info can be called with a ready descriptor.
//   - Maintain a graphics-state stack for q/Q (pushes/restores the full
//     interp state, including the embedded text state).
//   - Track CTM via cm (kept for completeness; text emission still uses
//     PDF-native baseline coords; multiplication into emitted points is
//     a Phase 3 concern — most generated PDFs use identity CTM around
//     simple BT/ET blocks).
//   - Delegate every BT/ET/Tm/Td/TD/T*/TL/Tj/TJ/'/" to text.apply_op.
//   - Stub all path/painting/color/line operators (Phase 3).
//
// We use `fn` for every helper and confine `var`/loop mutation to the
// outermost driver `pn`. Only the driver maintains `var` state because
// Lambda's procedural mode silently locks `var x = null` (see
// vibe/Lambda_Issues5.md #14/#15).

import util:    .util
import resolve: .resolve
import font:    .font
import text:    .text

// ============================================================
// State
// ============================================================
//
// State record:
//   { ctm:    [a b c d e f]  current transformation matrix
//     text:   <text.new_state record>
//     fonts:  list of {name, info} pairs (pre-resolved)
//   }
//
// The graphics-state stack (q/Q) is a list of these records held by the
// driver; every q pushes the current state, every Q pops the top.

fn new_state(fonts_resolved) {
    {
        ctm:   util.IDENTITY,
        text:  text.new_state(null),   // text.ls fonts param unused; we resolve here
        fonts: fonts_resolved
    }
}

fn _with_text(st, t) {
    { ctm: st.ctm, text: t, fonts: st.fonts }
}

fn _with_ctm(st, m) {
    { ctm: m, text: st.text, fonts: st.fonts }
}

// ============================================================
// Font lookup (linear scan over pre-resolved list)
// ============================================================

fn _lookup_resolved(fonts, name) {
    let hits = (for (p in fonts where p.name == name) p.info)
    if (len(hits) >= 1) { hits[0] } else { null }
}

// ============================================================
// Operand helper (mirror of text._num — kept local to avoid coupling
// to text.ls private helpers)
// ============================================================

fn _num(op) {
    if (op == null)        { 0.0 }
    else if (op is float)  { op }
    else if (op is int)    { float(op) }
    else                   { 0.0 }
}

// ============================================================
// Operator handlers (graphics state)
// ============================================================

fn _op_cm(st, ops) {
    if (len(ops) >= 6) {
        let m = [_num(ops[0]), _num(ops[1]), _num(ops[2]),
                 _num(ops[3]), _num(ops[4]), _num(ops[5])]
        _with_ctm(st, util.matrix_mul(m, st.ctm))
    }
    else { st }
}

// q / Q are handled in the driver because they touch the stack.

// Tf: resolve the font from the pre-built list and call text.set_font_info.
fn _op_Tf(st, ops) {
    let n = len(ops)
    let op0 = if (n >= 1) ops[0] else null
    let fname = if (op0 is map and op0.kind == "name") op0.value else st.text.font_name
    let fsize = if (n >= 2) _num(ops[1]) else st.text.font_size
    let info  = _lookup_resolved(st.fonts, fname)
    _with_text(st, text.set_font_info(st.text, fname, fsize, info))
}

// ============================================================
// Pre-resolution: walk ops once and resolve every distinct Tf name.
// ============================================================

fn _is_tf_name(op_record) {
    (op_record.op == "Tf")
        and (len(op_record.operands) >= 1)
        and (op_record.operands[0] is map)
        and (op_record.operands[0].kind == "name")
}

fn _list_contains(list, name) {
    let hits = (for (s in list where s == name) s)
    (len(hits) >= 1)
}

// Returns a list of unique font names referenced by Tf, in encounter order.
pn _collect_font_names(ops) {
    var seen = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let op = ops[i]
        if (_is_tf_name(op)) {
            let nm = op.operands[0].value
            if (not _list_contains(seen, nm)) {
                seen = seen ++ [nm]
            }
        }
        i = i + 1
    }
    return seen
}

pn _resolve_fonts(pdf, page, ops) {
    let names = _collect_font_names(ops)
    var out = []
    var i = 0
    let n = len(names)
    while (i < n) {
        let nm = names[i]
        let info = font.resolve_font(pdf, page, nm)
        out = out ++ [{ name: nm, info: info }]
        i = i + 1
    }
    return out
}

// ============================================================
// Driver
// ============================================================
//
// Walks the operator stream once. Returns the list of emitted SVG
// elements in render order.

pub pn render_page(pdf, page, ops, page_h) {
    let fonts = _resolve_fonts(pdf, page, ops)
    var st = new_state(fonts)
    var stack = []
    var out = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let opr  = ops[i].op
        let args = ops[i].operands

        if (opr == "q") {
            stack = stack ++ [st]
        }
        else if (opr == "Q") {
            let m = len(stack)
            if (m >= 1) {
                st = stack[m - 1]
                // pop the last element (no list slice; rebuild via comprehension)
                stack = (for (k, v in stack where k < (m - 1)) v)
            }
        }
        else if (opr == "cm") {
            st = _op_cm(st, args)
        }
        else if (opr == "Tf") {
            st = _op_Tf(st, args)
        }
        else {
            // Delegate every remaining text operator to text.apply_op.
            // Non-text operators (rg/RG/g/G/w/m/l/c/h/S/f/B/...) hit the
            // catch-all in text.apply_op which returns { state: st, emit: null }.
            let r = text.apply_op(st.text, opr, args, page_h)
            st = _with_text(st, r.state)
            if (r.emit != null) {
                var k = 0
                let me = len(r.emit)
                while (k < me) {
                    out = out ++ [r.emit[k]]
                    k = k + 1
                }
            }
        }
        i = i + 1
    }
    return out
}
