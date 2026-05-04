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
import shading: .shading
import coords:  .coords
import svg:     .svg

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

fn _with_clip(st, id) {
    st
}

// Wrap each emitted path in <g transform="matrix(ctm)"> so the outer
// y-flip group can mirror them upright. Skip the wrap when the CTM is
// the identity matrix — `matrix(1 0 0 1 0 0)` would just bloat output.
// Wrap each emitted path in <g transform="matrix(ctm)"> so the outer
// y-flip group can mirror them upright. Skip the wrap when the CTM is
// the identity matrix — `matrix(1 0 0 1 0 0)` would just bloat output.
//
// `emit` is a singleton array (path painters return [elem] or []), so
// we materialize the result as an array literal rather than a `for`
// comprehension — Lambda's `++` rejects array/list mixes.
fn _wrap_emit_with_ctm(emit, ctm, clip_id, has_clip) {
    if (len(emit) == 0) { emit }
    else {
        let needs_xform = not util.is_identity(ctm)
        let needs_clip  = (has_clip == 1)
        if (not needs_xform and not needs_clip) { emit }
        else if (needs_xform and needs_clip) {
            let xform = util.fmt_matrix(ctm)
            [<g transform: xform, 'clip-path': "url(#" ++ clip_id ++ ")"; emit[0]>]
        }
        else if (needs_xform) {
            let xform = util.fmt_matrix(ctm)
            [svg.group(xform, [emit[0]])]
        }
        else {
            [<g 'clip-path': "url(#" ++ clip_id ++ ")"; emit[0]>]
        }
    }
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
fn _op_RG(st, ops) {
    let c = color.from_rg_ops(ops)
    let st1 = _with_path(st, path.set_stroke_color(st.path, c))
    _with_text(st1, text.set_stroke(st1.text, c))
}
fn _op_g(st, ops)  {
    let c = color.from_g_ops(ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}
fn _op_G(st, ops)  {
    let c = color.from_g_ops(ops)
    let st1 = _with_path(st, path.set_stroke_color(st.path, c))
    _with_text(st1, text.set_stroke(st1.text, c))
}
fn _op_k(st, ops)  {
    let c = color.from_k_ops(ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}
fn _op_K(st, ops)  {
    let c = color.from_k_ops(ops)
    let st1 = _with_path(st, path.set_stroke_color(st.path, c))
    _with_text(st1, text.set_stroke(st1.text, c))
}

// sc/scn — set fill color in current colorspace (treat as text fill too).
fn _op_sc(st, ops) {
    let c = color.from_sc_ops(ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}
// SC/SCN — set stroke color in current colorspace.
fn _op_SC(st, ops) {
    let c = color.from_sc_ops(ops)
    let st1 = _with_path(st, path.set_stroke_color(st.path, c))
    _with_text(st1, text.set_stroke(st1.text, c))
}

fn _is_color_op(opr) {
    ((opr == "rg") or (opr == "RG") or (opr == "g") or (opr == "G")
        or (opr == "k") or (opr == "K")
        or (opr == "sc") or (opr == "SC")
        or (opr == "scn") or (opr == "SCN")
        or (opr == "cs") or (opr == "CS"))
}

fn _apply_color(st, opr, ops) {
    if      (opr == "rg") { _op_rg(st, ops) }
    else if (opr == "RG") { _op_RG(st, ops) }
    else if (opr == "g")  { _op_g(st, ops) }
    else if (opr == "G")  { _op_G(st, ops) }
    else if (opr == "k")  { _op_k(st, ops) }
    else if (opr == "K")  { _op_K(st, ops) }
    else if (opr == "sc"  or opr == "scn") { _op_sc(st, ops) }
    else if (opr == "SC"  or opr == "SCN") { _op_SC(st, ops) }
    // cs/CS just select the active colorspace; sc/scn following will
    // pick up the operand count. We currently ignore the actual space
    // (no Indexed/ICCBased lookup), which matches "fall back to device
    // family" rendering.
    else                  { st }
}

// ============================================================
// gs — apply named ExtGState dictionary
// ============================================================
//
// Honors only the alpha entries today (`ca` for fill, `CA` for stroke).
// Looks up `Resources/ExtGState/<name>` on the page; missing entries
// or non-numeric values are ignored.

fn _ext_gstate_dict(pdf, page, name) {
    let res = resolve.page_resources(pdf, page)
    let table = if (res and res.ExtGState) resolve.deref(pdf, res.ExtGState)
                else null
    if (table == null) { null }
    else { resolve.deref(pdf, table[name]) }
}

fn _alpha_of(d, key, fallback) {
    let v = if (d) d[key] else null
    if (v is float) { v }
    else if (v is int) { float(v) }
    else { fallback }
}

fn _gs_name(ops) {
    let n = len(ops)
    let op0 = if (n >= 1) ops[0] else null
    if (op0 is map and op0.kind == "name") { op0.value }
    else { null }
}

fn _apply_gs(st, ops, pdf, page) {
    let nm = _gs_name(ops)
    if (nm == null) { st }
    else {
        let d = _ext_gstate_dict(pdf, page, nm)
        if (d == null) { st }
        else {
            let fa = _alpha_of(d, "ca", st.path.fill_opacity)
            let sa = _alpha_of(d, "CA", st.path.stroke_opacity)
            _with_path(st, path.set_opacity(st.path, fa, sa))
        }
    }
}

// ============================================================
// No-op operators (acknowledge to avoid falling through to text)
// ============================================================
//
// PDF marker / metadata / Type-3-glyph operators we accept and ignore.
// Listing them explicitly keeps log noise down and prevents the text
// catch-all in text.apply_op from running for non-text operators.
fn _is_noop_op(opr) {
    ((opr == "MP") or (opr == "DP")
        or (opr == "BMC") or (opr == "BDC") or (opr == "EMC")
        or (opr == "BX") or (opr == "EX")
        or (opr == "ri") or (opr == "i")
        or (opr == "d0") or (opr == "d1"))
}

fn _media_box(page) { coords.media_box_rect(page) }

// ============================================================
// Pre-resolution: walk ops once and resolve every distinct Tf name.
// ============================================================

fn _is_tf_name(op_record) {
    ((op_record.op == "Tf")
        and (len(op_record.operands) >= 1)
        and (op_record.operands[0] is map)
        and (op_record.operands[0].kind == "name"))
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
    // Pending clip captured from the current path on W / W*; consumed
    // by the next path-painting operator. has_pending_clip flag avoids
    // string-sentinel pitfalls when the var was never assigned a real d.
    var pending_clip_d = " "
    var pending_clip_rule = "nonzero"
    var has_pending_clip = 0
    // Active clip-path SVG id (" " sentinel = none); set after a
    // W+paint cycle so subsequent emits get wrapped.
    var active_clip_id = " "
    var has_active_clip = 0
    // Counter for unique <clipPath>/<gradient> ids on this page.
    var def_ctr = 0
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
        else if (opr == "gs") {
            st = _apply_gs(st, args, pdf, page)
        }
        else if (opr == "W") {
            // Mark current path as pending non-zero clip. The clip is
            // established by the next painting op (which also clears
            // the path). Capture the `d` string now.
            let d = path.current_path_d(st.path)
            if (d != "" and len(d) > 0) {
                pending_clip_d = d
                pending_clip_rule = "nonzero"
                has_pending_clip = 1
            }
            else { st = st }
        }
        else if (opr == "W*") {
            let d = path.current_path_d(st.path)
            if (d != "" and len(d) > 0) {
                pending_clip_d = d
                pending_clip_rule = "evenodd"
                has_pending_clip = 1
            }
            else { st = st }
        }
        else if (opr == "sh") {
            // Shading fill — paint with the named gradient across the
            // current clip region (we approximate via a covering rect).
            let mb = _media_box(page)
            let s = shading.from_sh_op(pdf, page, st.ctm, args, mb.w, mb.h, def_ctr)
            def_ctr = def_ctr + 1
            if (len(s.defs) > 0) {
                var k = 0
                let me = len(s.defs)
                while (k < me) { paths = paths ++ [s.defs[k]]; k = k + 1 }
            }
            if (len(s.emit) > 0) {
                paths = paths ++ _wrap_emit_with_ctm(s.emit, st.ctm, active_clip_id, has_active_clip)
            }
        }
        else if (_is_noop_op(opr)) {
            // PDF marker / metadata / Type-3 ops — accept silently.
            // (non-empty body required; see Lambda gotcha.)
            st = st
        }
        else if (opr == "BI" or opr == "ID" or opr == "EI") {
            // BI/ID/EI handled at stream-tokenize time; ignore strays.
            st = st
        }
        else if (opr == "inline_image") {
            // Synthetic op from stream.ls when it skipped a BI..EI
            // segment. Emit a placeholder rect in the local CTM.
            let imgs = image.apply_inline(st.ctm, args)
            if (len(imgs) > 0) {
                paths = paths ++ _wrap_emit_with_ctm(imgs, st.ctm, active_clip_id, has_active_clip)
            }
        }
        else if (opr == "Do") {
            // Image / Form XObject placement. image.apply_do already
            // builds its own CTM-aware transform, so we append directly.
            let imgs = image.apply_do(pdf, page, st.ctm, args)
            if (len(imgs) > 0) {
                var k = 0
                let me = len(imgs)
                while (k < me) {
                    if (has_active_clip == 1) {
                        paths = paths ++ [<g 'clip-path': "url(#" ++ active_clip_id ++ ")"; imgs[k]>]
                    }
                    else {
                        paths = paths ++ [imgs[k]]
                    }
                    k = k + 1
                }
            }
        }
        else if (path.handles(opr)) {
            // Path construction / painting / line-state operator.
            let pr = path.apply_op(st.path, opr, args)
            st = _with_path(st, pr.state)
            // If a W/W* was set immediately before this op AND this op
            // paints (or no-ops via `n`), establish the clip now.
            let did_paint = (len(pr.emit) > 0) or (opr == "n")
            if (did_paint and has_pending_clip == 1) {
                let cid = "clip" ++ string(def_ctr)
                def_ctr = def_ctr + 1
                let cp = if (pending_clip_rule == "evenodd") {
                    <clipPath id: cid;
                        <path d: pending_clip_d, 'clip-rule': "evenodd">
                    >
                }
                else {
                    <clipPath id: cid;
                        <path d: pending_clip_d>
                    >
                }
                paths = paths ++ [cp]
                active_clip_id = cid
                has_active_clip = 1
                has_pending_clip = 0
            }
            if (len(pr.emit) > 0) {
                paths = paths ++ _wrap_emit_with_ctm(pr.emit, st.ctm, active_clip_id, has_active_clip)
            }
        }
        else {
            // Delegate every remaining operator to text.apply_op.
            // Unrecognized operators hit the catch-all there.
            let r = text.apply_op(st.text, st.ctm, opr, args, page_h)
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
    let deduped = text.dedupe_overprints(texts)
    return { texts: deduped, paths: paths }
}
