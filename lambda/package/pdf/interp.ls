// pdf/interp.ls — page-level content-stream interpreter
//
// Phase 2: tokenize → text rendering with q/Q/cm graphics-state stack.
// Phase 3: path construction (m/l/c/v/y/re/h), painting (S/s/f/F/f*/B/B*/b/b*/n),
//          color state (rg/RG/g/G/k/K), line state (w/J/j/M/d).
//
// Output:
//   render_page returns { texts: [...], paths: [...] }
//   - paths are in PDF user space; the caller must wrap them in the page-
//     level y-flip group so they read upright.
//   - texts are pre-flipped into SVG space by text.ls and sit outside the
//     y-flip group.
//
// We use `fn` for every helper and confine `var`/loop mutation to the
// outermost driver `pn`. Only the driver maintains `var` state because
// Lambda's procedural mode silently locks `var x = null` (see
// vibe/Lambda_Issues5.md #14/#15).

import util:    .util
import resolve: .resolve
import font:    .font
import text:    .text
import path:    .path
import color:   .color
import image:   .image

// ============================================================
// State
// ============================================================
//
// State record:
//   { ctm:    [a b c d e f]  current transformation matrix
//     text:   <text.new_state record>
//     path:   <path.new_state record>     // includes color + line state
//     fonts:  list of {name, info} pairs (pre-resolved)
//   }

fn new_state(fonts_resolved) {
    {
        ctm:   util.IDENTITY,
        text:  text.new_state(null),
        path:  path.new_state(),
        fonts: fonts_resolved
    }
}

fn _with_text(st, t) {
    { ctm: st.ctm, text: t, path: st.path, fonts: st.fonts }
}

fn _with_path(st, p) {
    { ctm: st.ctm, text: st.text, path: p, fonts: st.fonts }
}

fn _with_ctm(st, m) {
    { ctm: m, text: st.text, path: st.path, fonts: st.fonts }
}

// ============================================================
// Font lookup (linear scan over pre-resolved list)
// ============================================================

fn _lookup_resolved(fonts, name) {
    let hits = (for (p in fonts where p.name == name) p.info)
    if (len(hits) >= 1) { hits[0] } else { null }
}

// ============================================================
// Operand helper
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
// Color operators — update the path-state color slots
// ============================================================

fn _op_rg(st, ops) {
    let c = color.from_rg_ops(ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}
fn _op_RG(st, ops) { _with_path(st, path.set_stroke_color(st.path, color.from_rg_ops(ops))) }
fn _op_g(st, ops)  {
    let c = color.from_g_ops(ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}
fn _op_G(st, ops)  { _with_path(st, path.set_stroke_color(st.path, color.from_g_ops(ops))) }
fn _op_k(st, ops)  {
    let c = color.from_k_ops(ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}
fn _op_K(st, ops)  { _with_path(st, path.set_stroke_color(st.path, color.from_k_ops(ops))) }

fn _is_color_op(opr) {
    ((opr == "rg") or (opr == "RG") or (opr == "g") or (opr == "G")
        or (opr == "k") or (opr == "K"))
}

fn _apply_color(st, opr, ops) {
    if      (opr == "rg") { _op_rg(st, ops) }
    else if (opr == "RG") { _op_RG(st, ops) }
    else if (opr == "g")  { _op_g(st, ops) }
    else if (opr == "G")  { _op_G(st, ops) }
    else if (opr == "k")  { _op_k(st, ops) }
    else if (opr == "K")  { _op_K(st, ops) }
    else                  { st }
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
// Walks the operator stream once. Returns:
//   { texts: [...SVG-space text elements...],
//     paths: [...PDF-space path elements...] }

pub pn render_page(pdf, page, ops, page_h) {
    let fonts = _resolve_fonts(pdf, page, ops)
    var st = new_state(fonts)
    var stack = []
    var texts = []
    var paths = []
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
                stack = (for (k, v in stack where k < (m - 1)) v)
            }
        }
        else if (opr == "cm") {
            st = _op_cm(st, args)
        }
        else if (opr == "Tf") {
            st = _op_Tf(st, args)
        }
        else if (_is_color_op(opr)) {
            st = _apply_color(st, opr, args)
        }
        else if (opr == "Do") {
            // Image / Form XObject placement. We pass the live ctm so
            // the image element scales correctly. Emitted SVG sits inside
            // the page-level y-flip group alongside vector paths.
            let imgs = image.apply_do(pdf, page, st.ctm, args)
            if (len(imgs) > 0) {
                var k = 0
                let me = len(imgs)
                while (k < me) {
                    paths = paths ++ [imgs[k]]
                    k = k + 1
                }
            }
        }
        else if (path.handles(opr)) {
            // Path construction / painting / line-state operator.
            let pr = path.apply_op(st.path, opr, args)
            st = _with_path(st, pr.state)
            if (len(pr.emit) > 0) {
                var k = 0
                let me = len(pr.emit)
                while (k < me) {
                    paths = paths ++ [pr.emit[k]]
                    k = k + 1
                }
            }
        }
        else {
            // Delegate every remaining operator to text.apply_op.
            // Unrecognized operators hit the catch-all there.
            let r = text.apply_op(st.text, opr, args, page_h)
            st = _with_text(st, r.state)
            if (r.emit != null) {
                var k = 0
                let me = len(r.emit)
                while (k < me) {
                    texts = texts ++ [r.emit[k]]
                    k = k + 1
                }
            }
        }
        i = i + 1
    }
    return { texts: texts, paths: paths }
}
