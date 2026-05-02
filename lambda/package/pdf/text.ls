// pdf/text.ls — PDF text-object interpreter
//
// Phase 2 scope:
//   - Track BT/ET text state: text matrix Tm, text line matrix Tlm,
//     current font + size, leading, spacing.
//   - Emit one SVG <text> per Tj/TJ operator with the correct PDF baseline
//     position (page_h - Tm[5], Tm[4]) and font metadata.
//   - Decode literal and hex string operands via the font's to_unicode
//     CMap when present (pdf/font.ls).
//
// We render text WITHOUT the outer page-level y-flip for now: each text
// element computes its SVG y directly from page_height - Tm[5]. Vector
// graphics will be y-flipped separately when added.
//
// Implementation note: every dispatch helper is `fn` (not `pn`) because
// procedural functions in Lambda do not yield the value of `if/else`
// expressions, and `var` declared with `null` is permanently locked as
// null (silent assignment failure). State is purely value-based — each
// step returns a new record.

import util:  .util
import font:  .font

// ============================================================
// Operand helpers
// ============================================================

fn _num(op) {
    if (op == null)        { 0.0 }
    else if (op is float)  { op }
    else if (op is int)    { float(op) }
    else                   { 0.0 }
}

fn _decode_operand(op, font_info) {
    if (op == null) { "" }
    else if (op is map and op.kind == "string") {
        let cmap = if (font_info) font_info.to_unicode else null
        font.decode_literal(op.value, cmap)
    }
    else if (op is map and op.kind == "hex") {
        let cmap = if (font_info) font_info.to_unicode else null
        font.decode_hex(op.value, cmap)
    }
    else { "" }
}

fn _decode_tj_array(items, font_info) {
    let parts = (for (it in items
                      where (it is map and (it.kind == "string" or it.kind == "hex")))
                 _decode_operand(it, font_info))
    parts | join("")
}

// ============================================================
// State (use concrete typed defaults to avoid Lambda's null-trap)
// ============================================================

let _SENTINEL_NAME = " "   // non-empty so var won't lock as null

pub fn new_state(fonts) {
    {
        in_text:    false,
        tm:         util.IDENTITY,
        tlm:        util.IDENTITY,
        font_name:  _SENTINEL_NAME,
        font_size:  10.0,
        font_info:  null,
        leading:    0.0,
        char_space: 0.0,
        word_space: 0.0,
        hor_scale:  100.0,
        rise:       0.0,
        fonts:      fonts
    }
}

fn _with(st, tm, tlm, name, size, info, leading, in_text) {
    {
        in_text:    in_text,
        tm:         tm,
        tlm:        tlm,
        font_name:  name,
        font_size:  size,
        font_info:  info,
        leading:    leading,
        char_space: st.char_space,
        word_space: st.word_space,
        hor_scale:  st.hor_scale,
        rise:       st.rise,
        fonts:      st.fonts
    }
}

fn _set_tm(st, m) {
    _with(st, m, m, st.font_name, st.font_size, st.font_info, st.leading, st.in_text)
}

fn _set_in_text(st, flag) {
    _with(st, st.tm, st.tlm, st.font_name, st.font_size, st.font_info, st.leading, flag)
}

fn _lookup_font(fonts, name) {
    if (fonts == null) { null }
    else {
        let v = fonts[name]
        if (v is map) { v } else { null }
    }
}

fn _set_font(st, name, size) {
    let info = _lookup_font(st.fonts, name)
    _with(st, st.tm, st.tlm, name, size, info, st.leading, st.in_text)
}

// Public: set font with a pre-resolved info record (caller resolves
// via font.resolve_font). Lets interp.ls own all font resolution.
pub fn set_font_info(st, name, size, info) {
    _with(st, st.tm, st.tlm, name, size, info, st.leading, st.in_text)
}

fn _set_leading(st, l) {
    _with(st, st.tm, st.tlm, st.font_name, st.font_size, st.font_info, l, st.in_text)
}

// Td: Tlm := [1 0 0 1 tx ty] * Tlm; Tm := Tlm.
fn _move(st, tx, ty) {
    let delta = [1.0, 0.0, 0.0, 1.0, tx, ty]
    let new_tlm = util.matrix_mul(delta, st.tlm)
    _set_tm(st, new_tlm)
}

// ============================================================
// Emission
// ============================================================

fn _emit_text(st, page_h, content) {
    if (content == "" or content == null) { null }
    else {
        let fi = st.font_info
        let family = if (fi) fi.family else "sans-serif"
        let weight = if (fi) fi.weight else "normal"
        let style  = if (fi) fi.style  else "normal"
        let x = st.tm[4]
        let y = float(page_h) - st.tm[5]
        <text x: util.fmt_num(x), y: util.fmt_num(y),
              'font-family': family,
              'font-size':   util.fmt_num(st.font_size),
              'font-weight': weight,
              'font-style':  style;
            content
        >
    }
}

fn _emit_one(st, page_h, txt) {
    let el = _emit_text(st, page_h, txt)
    if (el) { [el] } else { [] }
}

// ============================================================
// Per-operator handlers
// ============================================================

fn _op_BT(st)  { { state: _set_in_text(_set_tm(st, util.IDENTITY), true), emit: null } }
fn _op_ET(st)  { { state: _set_in_text(st, false), emit: null } }

fn _op_Tf(st, ops) {
    let n = len(ops)
    let op0 = if (n >= 1) ops[0] else null
    let fname = if (op0 is map and op0.kind == "name") op0.value else st.font_name
    let fsize = if (n >= 2) _num(ops[1]) else st.font_size
    { state: _set_font(st, fname, fsize), emit: null }
}

fn _op_Tm(st, ops) {
    if (len(ops) >= 6) {
        let m = [_num(ops[0]), _num(ops[1]), _num(ops[2]),
                 _num(ops[3]), _num(ops[4]), _num(ops[5])]
        { state: _set_tm(st, m), emit: null }
    }
    else { { state: st, emit: null } }
}

fn _op_Td(st, ops) {
    if (len(ops) >= 2) { { state: _move(st, _num(ops[0]), _num(ops[1])), emit: null } }
    else               { { state: st, emit: null } }
}

fn _op_TD(st, ops) {
    if (len(ops) >= 2) {
        let tx = _num(ops[0])
        let ty = _num(ops[1])
        { state: _move(_set_leading(st, -ty), tx, ty), emit: null }
    }
    else { { state: st, emit: null } }
}

fn _op_Tstar(st) { { state: _move(st, 0.0, -st.leading), emit: null } }

fn _op_TL(st, ops) {
    if (len(ops) >= 1) { { state: _set_leading(st, _num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

fn _op_Tj(st, ops, page_h) {
    if (len(ops) >= 1) {
        let txt = _decode_operand(ops[0], st.font_info)
        { state: st, emit: _emit_one(st, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

fn _op_TJ(st, ops, page_h) {
    let op0 = if (len(ops) >= 1) ops[0] else null
    if (op0 is map and op0.kind == "array") {
        let txt = _decode_tj_array(op0.value, st.font_info)
        { state: st, emit: _emit_one(st, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

fn _op_quote(st, ops, page_h) {
    if (len(ops) >= 1) {
        let s1 = _move(st, 0.0, -st.leading)
        let txt = _decode_operand(ops[0], s1.font_info)
        { state: s1, emit: _emit_one(s1, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

fn _op_dquote(st, ops, page_h) {
    if (len(ops) >= 3) {
        let s1 = _move(st, 0.0, -st.leading)
        let txt = _decode_operand(ops[2], s1.font_info)
        { state: s1, emit: _emit_one(s1, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

// ============================================================
// Top dispatcher
// ============================================================

pub fn apply_op(st, opr, ops, page_h) {
    if      (opr == "BT") { _op_BT(st) }
    else if (opr == "ET") { _op_ET(st) }
    else if (opr == "Tf") { _op_Tf(st, ops) }
    else if (opr == "Tm") { _op_Tm(st, ops) }
    else if (opr == "Td") { _op_Td(st, ops) }
    else if (opr == "TD") { _op_TD(st, ops) }
    else if (opr == "T*") { _op_Tstar(st) }
    else if (opr == "TL") { _op_TL(st, ops) }
    else if (opr == "Tj") { _op_Tj(st, ops, page_h) }
    else if (opr == "TJ") { _op_TJ(st, ops, page_h) }
    else if (opr == "'")  { _op_quote(st, ops, page_h) }
    else if (opr == "\"") { _op_dquote(st, ops, page_h) }
    else                  { { state: st, emit: null } }
}

// ============================================================
// Driver — only mutation point
// ============================================================

pub pn render_text_ops(ops, fonts, page_h) {
    var st = new_state(fonts)
    var out = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let r = apply_op(st, ops[i].op, ops[i].operands, page_h)
        st = r.state
        if (r.emit != null) {
            var k = 0
            let m = len(r.emit)
            while (k < m) {
                out = out ++ [r.emit[k]]
                k = k + 1
            }
        }
        i = i + 1
    }
    return out
}
