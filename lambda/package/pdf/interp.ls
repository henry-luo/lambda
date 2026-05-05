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
import stream:  .stream

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
                fonts: fonts_resolved,
                fill_cs: "DeviceRGB",
                stroke_cs: "DeviceRGB"
    }
}

fn _with_text(st, t) {
        { ctm: st.ctm, text: t, path: st.path, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: st.stroke_cs }
}

fn _with_path(st, p) {
        { ctm: st.ctm, text: st.text, path: p, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: st.stroke_cs }
}

fn _with_ctm(st, m) {
        { ctm: m, text: st.text, path: st.path, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: st.stroke_cs }
}

fn _with_fill_cs(st, cs) {
        { ctm: st.ctm, text: st.text, path: st.path, fonts: st.fonts,
            fill_cs: cs, stroke_cs: st.stroke_cs }
}

fn _with_stroke_cs(st, cs) {
        { ctm: st.ctm, text: st.text, path: st.path, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: cs }
}

fn _with_clip(st, id) {
    st
}

fn _initial_run_state(fonts, init_ctm, inherited_st) {
    if (inherited_st == null) { _with_ctm(new_state(fonts), init_ctm) }
    else {
        {
            ctm:   init_ctm,
            text:  inherited_st.text,
            path:  path.clear_current_path(inherited_st.path),
            fonts: fonts,
            fill_cs: inherited_st.fill_cs,
            stroke_cs: inherited_st.stroke_cs
        }
    }
}

fn _safe_text_state(t) {
    if (t is map and t.font_size != null and t.tm != null and t.tlm != null) { t }
    else { text.new_state(null) }
}

fn _restore_state(saved) {
    {
        ctm:   saved.ctm,
        text:  _safe_text_state(saved.text),
        path:  saved.path,
        fonts: saved.fonts,
        fill_cs: saved.fill_cs,
        stroke_cs: saved.stroke_cs
    }
}

fn _font_resource_names(pdf, page) {
    let res = resolve.page_resources(pdf, page)
    let table = if (res and res.Font) resolve.deref(pdf, res.Font) else null
    if (table) { for (k, v in table) string(k) } else { [] }
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
pn _wrap_emit_with_ctm(emit, ctm, clip_ids) {
    if (len(emit) == 0) { return emit }
    var out = emit[0]
    if (not util.is_identity(ctm)) {
        out = svg.group(util.fmt_matrix(ctm), [out])
    }
    var i = len(clip_ids) - 1
    while (i >= 0) {
        out = <g 'clip-path': "url(#" ++ clip_ids[i] ++ ")"; out>
        i = i - 1
    }
    return [out]
}

// ============================================================
// Font lookup (linear scan over pre-resolved list)
// ============================================================

fn _lookup_resolved(fonts, name) {
    let hits = (for (p in fonts where p.name == name) p.info)
    if (len(hits) >= 1) { hits[0] } else { null }
}

fn _runtime_font_info(info) {
    if (info == null) { null }
    else {
        {
            name:       info.name,
            family:     info.family,
            weight:     info.weight,
            style:      info.style,
            to_unicode: info.to_unicode,
            encoding:   info.encoding,
            widths:     info.widths,
            first_char: info.first_char,
            last_char:  info.last_char
        }
    }
}

// ============================================================
// Operand helper
// ============================================================

// ============================================================
// Operator handlers (graphics state)
// ============================================================

fn _op_cm(st, ops) {
    if (len(ops) >= 6) {
        let m = [util.num(ops[0]), util.num(ops[1]), util.num(ops[2]),
                 util.num(ops[3]), util.num(ops[4]), util.num(ops[5])]
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
    let fsize = if (n >= 2) util.num(ops[1]) else st.text.font_size
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

fn _op_cs(st, ops) {
    let op0 = if (len(ops) >= 1) ops[0] else null
    if (op0 != null) { _with_fill_cs(st, op0) } else { st }
}

fn _op_CS(st, ops) {
    let op0 = if (len(ops) >= 1) ops[0] else null
    if (op0 != null) { _with_stroke_cs(st, op0) } else { st }
}

fn _op_sc_with_space(st, ops, pdf, page) {
    let c = color.from_ops_in_space(pdf, page, st.fill_cs, ops)
    let st1 = _with_path(st, path.set_fill_color(st.path, c))
    _with_text(st1, text.set_fill(st1.text, c))
}

fn _op_SC_with_space(st, ops, pdf, page) {
    let c = color.from_ops_in_space(pdf, page, st.stroke_cs, ops)
    let st1 = _with_path(st, path.set_stroke_color(st.path, c))
    _with_text(st1, text.set_stroke(st1.text, c))
}

fn _set_pattern_fill(st, p) {
    let st1 = _with_path(st, path.set_fill_color(st.path, p.fill))
    _with_text(st1, text.set_fill(st1.text, p.fill))
}

fn _set_pattern_stroke(st, p) {
    let st1 = _with_path(st, path.set_stroke_color(st.path, p.fill))
    _with_text(st1, text.set_stroke(st1.text, p.fill))
}

fn _is_color_op(opr) {
    ((opr == "rg") or (opr == "RG") or (opr == "g") or (opr == "G")
        or (opr == "k") or (opr == "K")
        or (opr == "sc") or (opr == "SC")
        or (opr == "scn") or (opr == "SCN")
        or (opr == "cs") or (opr == "CS"))
}

fn _apply_color(st, opr, ops, pdf, page) {
    if      (opr == "rg") { _op_rg(st, ops) }
    else if (opr == "RG") { _op_RG(st, ops) }
    else if (opr == "g")  { _op_g(st, ops) }
    else if (opr == "G")  { _op_G(st, ops) }
    else if (opr == "k")  { _op_k(st, ops) }
    else if (opr == "K")  { _op_K(st, ops) }
    else if (opr == "cs") { _op_cs(st, ops) }
    else if (opr == "CS") { _op_CS(st, ops) }
    else if (opr == "sc"  or opr == "scn") { _op_sc_with_space(st, ops, pdf, page) }
    else if (opr == "SC"  or opr == "SCN") { _op_SC_with_space(st, ops, pdf, page) }
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

fn _is_text_op(opr) {
    ((opr == "BT") or (opr == "ET") or (opr == "Tf")
        or (opr == "Tm") or (opr == "Td") or (opr == "TD")
        or (opr == "T*") or (opr == "TL")
        or (opr == "Tc") or (opr == "Tw") or (opr == "Tz")
        or (opr == "Ts") or (opr == "Tr")
        or (opr == "Tj") or (opr == "TJ")
        or (opr == "'") or (opr == "\""))
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
        out = out ++ [{ name: nm, info: _runtime_font_info(info) }]
        i = i + 1
    }
    return out
}

pub pn resolve_page_fonts(pdf, page) {
    let names = _font_resource_names(pdf, page)
    var out = []
    var i = 0
    let n = len(names)
    while (i < n) {
        let nm = names[i]
        let info = font.resolve_font(pdf, page, nm)
        out = out ++ [{ name: nm, info: _runtime_font_info(info) }]
        i = i + 1
    }
    return out
}

// ============================================================
// Form XObject expansion (byte-level pre-pass)
// ============================================================
//
// Lambda's `pn → pn` array marshaling has trouble with parsed-op
// records (see vibe/Lambda_Issues5.md #22), so we expand Form XObject
// references entirely at the byte level: every `/<name> Do` is rewritten
// to a synthetic `q <matrix> cm <form-bytes> Q` block before the
// content stream is tokenized. This way the interpreter sees one flat
// op stream and the existing q/Q machinery handles state save/restore.

fn _matrix_str(m) {
    util.fmt_num(m[0]) ++ " " ++ util.fmt_num(m[1]) ++ " " ++
    util.fmt_num(m[2]) ++ " " ++ util.fmt_num(m[3]) ++ " " ++
    util.fmt_num(m[4]) ++ " " ++ util.fmt_num(m[5])
}

fn _do_replace(cur, nm, mstr, data) {
    let needle = "/" ++ nm ++ " Do"
    let replacement = "q " ++ mstr ++ " cm\n" ++ data ++ "\nQ"
    replace(cur, needle, replacement)
}

pub pn expand_forms_in_bytes(pdf, page, bytes) {
    let res = resolve.page_resources(pdf, page)
    if (res == null) { return bytes }
    let xo = res.XObject
    if (xo == null) { return bytes }
    let xo_names = for (k, v in xo) string(k)
    var cur = bytes
    var i = 0
    let n = len(xo_names)
    while (i < n) {
        let nm = xo_names[i]
        let fc = image.form_content(pdf, page, nm)
        let has_data = (fc != null and fc.data != "")
        if (has_data) {
            let mstr = _matrix_str(fc.matrix)
            cur = _do_replace(cur, nm, mstr, fc.data)
        }
        i = i + 1
    }
    return cur
}

// ============================================================
// Form XObject expansion helpers (used inline by _run_ops below)
// ============================================================

fn _is_form_do(op, pdf, page) {
    if (op.op != "Do")                     { false }
    else if (len(op.operands) < 1)         { false }
    else if (not (op.operands[0] is map))  { false }
    else if (op.operands[0].kind != "name") { false }
    else {
        let xo = image.lookup_xobject(pdf, page, op.operands[0].value)
        (xo != null) and (xo.kind == "form")
    }
}

fn _do_name(ops) {
    if (len(ops) == 0) { null }
    else {
        let op0 = ops[0]
        if (op0 is map and op0.kind == "name") { op0.value }
        else { null }
    }
}

fn _form_content_for_do(pdf, page, ops) {
    let nm = _do_name(ops)
    if (nm == null) { null }
    else { image.form_content(pdf, page, nm) }
}

fn _clip_prefix_for_do(base, ops, index) {
    let nm = _do_name(ops)
    if (nm == null) { base ++ "_do" ++ string(index) ++ "_" }
    else { base ++ "_" ++ nm ++ "_" ++ string(index) ++ "_" }
}

fn _is_clip_def(el) {
    if (not (el is element)) { false }
    else { string(name(el)) == "clipPath" }
}

fn _is_transparency_group(fc) {
    if (fc == null or fc.dict == null or fc.dict.Group == null) { false }
    else { fc.dict.Group.S == "Transparency" }
}

fn _form_group_opacity(st, fc) {
    if (not _is_transparency_group(fc)) { 1.0 }
    else if (st.path.fill_opacity < 1.0) { st.path.fill_opacity }
    else if (st.path.stroke_opacity < 1.0) { st.path.stroke_opacity }
    else { 1.0 }
}

fn _form_bounds_attr(fc) {
    if (fc == null or fc.dict == null or not (fc.dict.BBox is array) or len(fc.dict.BBox) < 4) { "" }
    else {
        let x0 = util.num(fc.dict.BBox[0]); let y0 = util.num(fc.dict.BBox[1])
        let x1 = util.num(fc.dict.BBox[2]); let y1 = util.num(fc.dict.BBox[3])
        (util.fmt_num(x0) ++ " " ++ util.fmt_num(y0) ++ " " ++
         util.fmt_num(x1 - x0) ++ " " ++ util.fmt_num(y1 - y0))
    }
}

fn _page_media_box_value(page) {
    if (page and page.dict and page.dict.MediaBox) { page.dict.MediaBox }
    else if (page and page.MediaBox) { page.MediaBox }
    else { null }
}

fn _form_resources(fc) {
    if (fc != null and fc.dict != null and fc.dict.Resources != null) { fc.dict.Resources }
    else { null }
}

fn _form_page(page, fc) {
    let res = _form_resources(fc)
    if (res == null) { page }
    else {
        { dict: { Resources: res, MediaBox: _page_media_box_value(page) },
          MediaBox: _page_media_box_value(page),
          resources: res }
    }
}

fn _form_child_state(st, fc) {
    if (_form_group_opacity(st, fc) < 1.0) {
        _with_path(st, path.set_opacity(st.path, 1.0, 1.0))
    }
    else { st }
}

fn _form_group(children, opacity_value, bounds_attr) {
    if (opacity_value < 1.0 and bounds_attr != "") {
        <g opacity: util.fmt_num(opacity_value), 'data-pdf-bounds': bounds_attr;
            for (c in children) c
        >
    }
    else if (opacity_value < 1.0) {
        <g opacity: util.fmt_num(opacity_value);
            for (c in children) c
        >
    }
    else {
        <g;
            for (c in children) c
        >
    }
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
    let r = _run_ops(pdf, page, ops, util.IDENTITY, fonts, page_h)
    let texts = [for (t in r.texts) t]
    let paths = [for (p in r.paths) p]
    return { texts: texts, paths: paths }
}

pub pn render_page_with_fonts(pdf, page, ops, page_h, fonts) {
    let r = _run_ops(pdf, page, ops, util.IDENTITY, fonts, page_h)
    let texts = [for (t in r.texts) t]
    let paths = [for (p in r.paths) p]
    return { texts: texts, paths: paths }
}

// Operator-walk loop, factored out so Form XObject `Do` operators can
// recursively interpret a sub-content-stream with its own CTM and font
// pool. Returns { texts, paths } — caller decides whether to dedupe.
pn _run_ops(pdf, page, ops, init_ctm, fonts, page_h) {
    return _run_ops_with_clip_prefix(pdf, page, ops, init_ctm, fonts, page_h, "clip")
}

pn _run_ops_with_clip_prefix(pdf, page, ops, init_ctm, fonts, page_h, clip_prefix) {
    return _run_ops_with_state(pdf, page, ops, init_ctm, fonts, page_h, clip_prefix, null)
}

pn _run_ops_with_state(pdf, page, ops, init_ctm, fonts, page_h, clip_prefix, inherited_st) {
    var st = _initial_run_state(fonts, init_ctm, inherited_st)
    var stack = []
    var texts = []
    var paths = []
    // Pending clip captured from the current path on W / W*; consumed
    // by the next path-painting operator. has_pending_clip flag avoids
    // string-sentinel pitfalls when the var was never assigned a real d.
    var pending_clip_d = " "
    var pending_clip_rule = "nonzero"
    var has_pending_clip = 0
    // Active clip-path SVG ids. PDF clips are cumulative intersections, so
    // each W/W* appends a new clip until the graphics state is restored.
    var active_clip_ids = []
    var fill_pattern_name = " "
    var fill_pattern_id = " "
    var has_fill_pattern = 0
    var fill_pattern_emitted = 0
    // Counter for unique <clipPath>/<gradient> ids on this page.
    var def_ctr = 0
    var emitted_pattern_ids = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let opr  = ops[i].op
        let operands = ops[i].operands

        if (opr == "q") {
            stack = stack ++ [{
                st: st,
                pending_clip_d: pending_clip_d,
                pending_clip_rule: pending_clip_rule,
                has_pending_clip: has_pending_clip,
                active_clip_ids: active_clip_ids,
                fill_pattern_name: fill_pattern_name,
                fill_pattern_id: fill_pattern_id,
                has_fill_pattern: has_fill_pattern,
                fill_pattern_emitted: fill_pattern_emitted,
                fill_cs: st.fill_cs,
                stroke_cs: st.stroke_cs
            }]
        }
        else if (opr == "Q") {
            let m = len(stack)
            if (m >= 1) {
                let saved = stack[m - 1]
                st = _restore_state(saved.st)
                pending_clip_d = saved.pending_clip_d
                pending_clip_rule = saved.pending_clip_rule
                has_pending_clip = saved.has_pending_clip
                active_clip_ids = saved.active_clip_ids
                fill_pattern_name = saved.fill_pattern_name
                fill_pattern_id = saved.fill_pattern_id
                has_fill_pattern = saved.has_fill_pattern
                fill_pattern_emitted = saved.fill_pattern_emitted
                st = _with_fill_cs(st, saved.fill_cs)
                st = _with_stroke_cs(st, saved.stroke_cs)
                stack = (for (k, v in stack where k < (m - 1)) v)
            }
        }
        else if (opr == "cm") {
            st = _op_cm(st, operands)
        }
        else if (opr == "Tf") {
            st = _op_Tf(st, operands)
        }
        else if (_is_text_op(opr)) {
            let tr = text.apply_op(st.text, st.ctm, opr, operands, page_h)
            st = _with_text(st, tr.state)
            if (tr.emit != null) {
                var tk = 0
                let tn = len(tr.emit)
                while (tk < tn) {
                    texts = texts ++ [tr.emit[tk]]
                    tk = tk + 1
                }
            }
        }
        else if (_is_color_op(opr)) {
            var pname = "unused"
            var has_pattern = 0
            if (opr == "scn" or opr == "SCN") {
                let ds = string(operands)
                let dp = index_of(ds, "value")
                if (dp >= 0) {
                    let dr = slice(ds, dp + 8, len(ds))
                    let name_parts = split(dr, "\"")
                    if (len(name_parts) >= 1) {
                        pname = name_parts[0]
                        has_pattern = 1
                    }
                }
            }
            if (has_pattern == 1) {
                let pid = clip_prefix ++ "pat" ++ string(def_ctr)
                def_ctr = def_ctr + 1
                let pat = { defs: [], fill: "url(#" ++ pid ++ ")" }
                if (opr == "scn") {
                    st = _set_pattern_fill(st, pat)
                    fill_pattern_name = pname
                    fill_pattern_id = pid
                    has_fill_pattern = 1
                    fill_pattern_emitted = 0
                }
                else { st = _set_pattern_stroke(st, pat) }
            }
            else {
                st = _apply_color(st, opr, operands, pdf, page)
                if (opr == "rg" or opr == "g" or opr == "k" or opr == "sc") {
                    has_fill_pattern = 0
                    fill_pattern_emitted = 0
                }
            }
        }
        else if (opr == "gs") {
            st = _apply_gs(st, operands, pdf, page)
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
            let s = shading.from_sh_op(pdf, page, st.ctm, operands, mb.w, mb.h, def_ctr)
            def_ctr = def_ctr + 1
            if (len(s.defs) > 0) {
                var k = 0
                let me = len(s.defs)
                while (k < me) { paths = paths ++ [s.defs[k]]; k = k + 1 }
            }
            if (len(s.emit) > 0) {
                paths = paths ++ _wrap_emit_with_ctm(s.emit, st.ctm, active_clip_ids)
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
            let imgs = image.apply_inline(st.ctm, operands)
            if (len(imgs) > 0) {
                paths = paths ++ _wrap_emit_with_ctm(imgs, st.ctm, active_clip_ids)
            }
        }
        else if (opr == "Do") {
            // Image / Form XObject placement. Interpret Form streams in a
            // smaller recursive pass instead of flattening them into the page
            // stream; large flat op arrays can corrupt later operands.
            let fc = _form_content_for_do(pdf, page, operands)
            let has_form = (fc != null and fc.data != "")
            if (has_form) {
                let form_ops = stream.parse_content_stream(fc.data)
                let form_ctm = util.matrix_mul(fc.matrix, st.ctm)
                let form_page = _form_page(page, fc)
                let form_fonts = _resolve_fonts(pdf, form_page, form_ops) ++ fonts
                let sub_prefix = _clip_prefix_for_do(clip_prefix, operands, i)
                let group_opacity = _form_group_opacity(st, fc)
                let child_st = _form_child_state(st, fc)
                let sub = _run_ops_with_state(pdf, form_page, form_ops, form_ctm, form_fonts, page_h, sub_prefix, child_st)
                var tj = 0
                let tn = len(sub.texts)
                while (tj < tn) {
                    texts = texts ++ [sub.texts[tj]]
                    tj = tj + 1
                }
                var defs = []
                var draws = []
                var k = 0
                let me = len(sub.paths)
                while (k < me) {
                    if (_is_clip_def(sub.paths[k])) {
                        defs = defs ++ [sub.paths[k]]
                    }
                    else {
                        draws = draws ++ [sub.paths[k]]
                    }
                    k = k + 1
                }
                paths = paths ++ defs
                if (len(draws) > 0) {
                    let grouped = _form_group(draws, group_opacity, _form_bounds_attr(fc))
                    paths = paths ++ _wrap_emit_with_ctm([grouped], util.IDENTITY, active_clip_ids)
                }
            }
            else {
                let imgs = image.apply_do(pdf, page, st.ctm, operands)
                if (len(imgs) > 0) {
                    var k = 0
                    let me = len(imgs)
                    while (k < me) {
                        paths = paths ++ _wrap_emit_with_ctm([imgs[k]], util.IDENTITY, active_clip_ids)
                        k = k + 1
                    }
                }
            }
        }
        else if (path.handles(opr)) {
            // Path construction / painting / line-state operator.
            let pr = path.apply_op(st.path, opr, operands)
            st = _with_path(st, pr.state)
            // If a W/W* was set immediately before this op AND this op
            // paints (or no-ops via `n`), establish the clip now.
            let did_paint = (len(pr.emit) > 0) or (opr == "n")
            if (did_paint and has_pending_clip == 1) {
                let cid = clip_prefix ++ string(def_ctr)
                def_ctr = def_ctr + 1
                let clip_xform = util.fmt_matrix(st.ctm)
                let cp = if (pending_clip_rule == "evenodd") {
                    <clipPath id: cid;
                        <path d: pending_clip_d, transform: clip_xform, 'clip-rule': "evenodd">
                    >
                }
                else {
                    <clipPath id: cid;
                        <path d: pending_clip_d, transform: clip_xform>
                    >
                }
                paths = paths ++ [cp]
                active_clip_ids = active_clip_ids ++ [cid]
                has_pending_clip = 0
            }
            if (len(pr.emit) > 0) {
                if (has_fill_pattern == 1 and fill_pattern_emitted == 0 and not _list_contains(emitted_pattern_ids, fill_pattern_id)) {
                    let pat = shading.from_pattern_fill(pdf, page, fill_pattern_name, fill_pattern_id, st.ctm)
                    var pk = 0
                    let pn = len(pat.defs)
                    while (pk < pn) { paths = paths ++ [pat.defs[pk]]; pk = pk + 1 }
                    emitted_pattern_ids = emitted_pattern_ids ++ [fill_pattern_id]
                    fill_pattern_emitted = 1
                }
                paths = paths ++ _wrap_emit_with_ctm(pr.emit, st.ctm, active_clip_ids)
            }
        }
        else {
            st = st
        }
        i = i + 1
    }
    let deduped = text.dedupe_overprints(texts)
    return { texts: deduped, paths: paths }
}
