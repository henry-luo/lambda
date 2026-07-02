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
                fonts: fonts_resolved,
                fill_cs: "DeviceRGB",
                stroke_cs: "DeviceRGB",
                soft_mask: null
    }
}

fn _with_text(st, t) {
        { ctm: st.ctm, text: t, path: st.path, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: st.stroke_cs, soft_mask: st.soft_mask }
}

fn _with_path(st, p) {
        { ctm: st.ctm, text: st.text, path: p, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: st.stroke_cs, soft_mask: st.soft_mask }
}

fn _with_ctm(st, m) {
        { ctm: m, text: st.text, path: st.path, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: st.stroke_cs, soft_mask: st.soft_mask }
}

fn _with_fill_cs(st, cs) {
        { ctm: st.ctm, text: st.text, path: st.path, fonts: st.fonts,
            fill_cs: cs, stroke_cs: st.stroke_cs, soft_mask: st.soft_mask }
}

fn _with_stroke_cs(st, cs) {
        { ctm: st.ctm, text: st.text, path: st.path, fonts: st.fonts,
            fill_cs: st.fill_cs, stroke_cs: cs, soft_mask: st.soft_mask }
}

fn _with_soft_mask(st, soft_mask) {
    { ctm: st.ctm, text: st.text, path: st.path, fonts: st.fonts,
      fill_cs: st.fill_cs, stroke_cs: st.stroke_cs, soft_mask: soft_mask }
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
            stroke_cs: inherited_st.stroke_cs,
            soft_mask: inherited_st.soft_mask
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
        stroke_cs: saved.stroke_cs,
        soft_mask: saved.soft_mask
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
fn _wrap_clip_ids(out, clip_ids, i) {
    if (i < 0) { out }
    else { _wrap_clip_ids(<g 'clip-path': "url(#" ++ clip_ids[i] ++ ")"; out>, clip_ids, i - 1) }
}

fn _wrap_emit_with_ctm(emit, ctm, clip_ids) {
    if (len(emit) == 0) { emit }
    else {
        let base = if (not util.is_identity(ctm)) { svg.group(util.fmt_matrix(ctm), [emit[0]]) }
                   else { emit[0] }
        [_wrap_clip_ids(base, clip_ids, len(clip_ids) - 1)]
    }
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
            last_char:  info.last_char,
            cid_widths: info.cid_widths,
            default_width: info.default_width
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

fn _pattern_name_from_ops(ops) {
    if (len(ops) < 1) { null }
    else {
        let op0 = ops[len(ops) - 1]
        if (op0 is map and op0.kind == "name") { op0.value }
        else { null }
    }
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
// Honors alpha entries (`ca` for fill, `CA` for stroke) and the common
// line-state entries (`LW`, `LC`, `LJ`, `ML`, `D`). Looks up
// `Resources/ExtGState/<name>` on the page; missing or malformed values
// are ignored.

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

fn _clamp_unit(v) {
    if (v < 0.0) { 0.0 }
    else if (v > 1.0) { 1.0 }
    else { v }
}

fn _first_component(v, fallback) {
    if (v is array and len(v) >= 1) { util.num(v[0]) }
    else if (v is int or v is float) { util.num(v) }
    else { fallback }
}

fn _function_mid_gray(pdf, fn_ref) {
    let func = resolve.deref(pdf, fn_ref)
    if (func == null) { 1.0 }
    else {
        let c0 = _first_component(func.C0, 0.0)
        let c1 = _first_component(func.C1, 1.0)
        _clamp_unit((c0 + c1) / 2.0)
    }
}

fn _first_shading_name_loop(ops, i, n) {
    if (i >= n) { null }
    else {
        let op = ops[i]
        if (op.op == "sh" and len(op.operands) >= 1 and
                op.operands[0] is map and op.operands[0].kind == "name") {
            op.operands[0].value
        }
        else { _first_shading_name_loop(ops, i + 1, n) }
    }
}

fn _first_shading_name(bytes) {
    let ops = pdf_parse_content_stream(bytes)
    _first_shading_name_loop(ops, 0, len(ops))
}

fn _soft_mask_group_opacity(pdf, group) {
    let dict = if (group and group.dictionary) { group.dictionary } else { group }
    let bytes = if (group and group.text_data != null) { group.text_data }
                else if (group and group.data != null) { group.data }
                else { "" }
    let nm = _first_shading_name(bytes)
    let table = if (dict and dict.Resources and dict.Resources.Shading) { dict.Resources.Shading } else { null }
    let sh = if (nm != null and table != null) { resolve.deref(pdf, table[nm]) } else { null }
    if (sh != null and sh.Function != null) { _function_mid_gray(pdf, sh.Function) }
    else { 1.0 }
}

fn _soft_mask_group_linear(pdf, group) {
    let dict = if (group and group.dictionary) { group.dictionary } else { group }
    let bytes = if (group and group.text_data != null) { group.text_data }
                else if (group and group.data != null) { group.data }
                else { "" }
    let nm = _first_shading_name(bytes)
    let table = if (dict and dict.Resources and dict.Resources.Shading) { dict.Resources.Shading } else { null }
    let sh = if (nm != null and table != null) { resolve.deref(pdf, table[nm]) } else { null }
    if (sh != null and sh.ShadingType == 2 and sh.Function != null and sh.Coords is array and len(sh.Coords) >= 4) {
        let func = resolve.deref(pdf, sh.Function)
        if (func != null) {
            {
                kind: "axial_gray",
                x0: util.num(sh.Coords[0]),
                y0: util.num(sh.Coords[1]),
                x1: util.num(sh.Coords[2]),
                y1: util.num(sh.Coords[3]),
                a0: _clamp_unit(_first_component(func.C0, 0.0)),
                a1: _clamp_unit(_first_component(func.C1, 1.0))
            }
        }
        else { null }
    }
    else { null }
}

fn _soft_mask_raw(pdf, d) {
    if (d) { d.SMask } else { null }
}

fn _soft_mask_linear(pdf, d) {
    let raw = _soft_mask_raw(pdf, d)
    if (raw == null or (raw is string and raw == "None")) { null }
    else {
        let sm = resolve.deref(pdf, raw)
        if (sm == null) { null }
        else {
            let subtype = if (sm.S != null) { string(sm.S) } else { "" }
            let group = if (sm.G != null) { resolve.deref(pdf, sm.G) } else { null }
            if ((subtype == "Luminosity" or subtype == "Alpha") and group != null) {
                _soft_mask_group_linear(pdf, group)
            }
            else { null }
        }
    }
}

fn _soft_mask_opacity(pdf, d) {
    let raw = _soft_mask_raw(pdf, d)
    if (raw == null or (raw is string and raw == "None")) { 1.0 }
    else {
        let sm = resolve.deref(pdf, raw)
        if (sm == null) { 1.0 }
        else {
            let subtype = if (sm.S != null) { string(sm.S) } else { "" }
            let group = if (sm.G != null) { resolve.deref(pdf, sm.G) } else { null }
            if ((subtype == "Luminosity" or subtype == "Alpha") and group != null) {
                _soft_mask_group_opacity(pdf, group)
            }
            else { 1.0 }
        }
    }
}

fn _num_of(d, key, fallback) {
    let v = if (d) d[key] else null
    if (v is float) { v }
    else if (v is int) { float(v) }
    else { fallback }
}

fn _int_of(d, key, fallback) {
    let v = if (d) d[key] else null
    if (v is int) { v }
    else if (v is float) { int(v) }
    else { fallback }
}

fn _dash_numbers(raw) {
    if (raw is map and raw.kind == "array") {
        for (n in raw.value where (n is int or n is float)) util.num(n)
    }
    else if (raw is array) {
        for (n in raw where (n is int or n is float)) util.num(n)
    }
    else { [] }
}

fn _apply_gs_line_state(p, d) {
    let p1 = if (d.LW != null) { path.set_line_width(p, _num_of(d, "LW", p.line_width)) } else { p }
    let p2 = if (d.LC != null) { path.set_line_cap(p1, _int_of(d, "LC", p1.line_cap)) } else { p1 }
    let p3 = if (d.LJ != null) { path.set_line_join(p2, _int_of(d, "LJ", p2.line_join)) } else { p2 }
    let p4 = if (d.ML != null) { path.set_miter_limit(p3, _num_of(d, "ML", p3.miter_limit)) } else { p3 }
    if (d.D != null and d.D is array and len(d.D) >= 2) {
        path.set_dash(p4, _dash_numbers(d.D[0]), util.num(d.D[1]))
    }
    else { p4 }
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
            let sm = _soft_mask_linear(pdf, d)
            let ma = if (sm != null) { 1.0 } else { _soft_mask_opacity(pdf, d) }
            let fa = _clamp_unit(_alpha_of(d, "ca", st.path.fill_opacity) * ma)
            let sa = _clamp_unit(_alpha_of(d, "CA", st.path.stroke_opacity) * ma)
            let p1 = path.set_opacity(st.path, fa, sa)
            let p2 = _apply_gs_line_state(p1, d)
            let st1 = _with_soft_mask(_with_path(st, p2), sm)
            _with_text(st1, text.set_opacity(st1.text, fa, sa))
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
fn _collect_font_names_loop(ops, i, n, seen) {
    if (i >= n) { seen }
    else {
        let op = ops[i]
        if (_is_tf_name(op)) {
            let nm = op.operands[0].value
            if (not _list_contains(seen, nm)) {
                _collect_font_names_loop(ops, i + 1, n, seen ++ [nm])
            }
            else { _collect_font_names_loop(ops, i + 1, n, seen) }
        }
        else { _collect_font_names_loop(ops, i + 1, n, seen) }
    }
}

fn _collect_font_names(ops) {
    _collect_font_names_loop(ops, 0, len(ops), [])
}

fn _resolve_fonts(pdf, page, ops) {
    let names = _collect_font_names(ops)
    [for (nm in names) { name: nm, info: _runtime_font_info(font.resolve_font(pdf, page, nm)) }]
}

pub fn resolve_page_fonts(pdf, page) {
    let names = _font_resource_names(pdf, page)
    [for (nm in names) { name: nm, info: _runtime_font_info(font.resolve_font(pdf, page, nm)) }]
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

fn _expand_forms_loop(pdf, page, xo_names, i, n, cur) {
    if (i >= n) { cur }
    else {
        let nm = xo_names[i]
        let fc = image.form_content(pdf, page, nm)
        let has_data = (fc != null and fc.data != "")
        let next = if (has_data) { _do_replace(cur, nm, _matrix_str(fc.matrix), fc.data) }
                   else { cur }
        _expand_forms_loop(pdf, page, xo_names, i + 1, n, next)
    }
}

pub fn expand_forms_in_bytes(pdf, page, bytes) {
    let res = resolve.page_resources(pdf, page)
    if (res == null) { bytes }
    else {
        let xo = res.XObject
        if (xo == null) { bytes }
        else {
            let xo_names = [for (k, v in xo) string(k)]
            _expand_forms_loop(pdf, page, xo_names, 0, len(xo_names), bytes)
        }
    }
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
    if (nm == null) { base ++ "_do" ++ (index) ++ "_" }
    else { base ++ "_" ++ nm ++ "_" ++ (index) ++ "_" }
}

fn _is_clip_def(el) {
    if (not (el is element)) { false }
    else { string(name(el)) == "clipPath" }
}

fn _form_group_dict(pdf, fc) {
    if (fc == null or fc.dict == null or fc.dict.Group == null) { null }
    else { resolve.deref(pdf, fc.dict.Group) }
}

fn _is_transparency_group(pdf, fc) {
    let g = _form_group_dict(pdf, fc)
    if (g == null) { false }
    else { g.S == "Transparency" }
}

fn _form_group_opacity(pdf, st, fc) {
    if (not _is_transparency_group(pdf, fc)) { 1.0 }
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

fn _form_child_state(pdf, st, fc) {
    if (_form_group_opacity(pdf, st, fc) < 1.0) {
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

fn _image_opacity_group(children, opacity_value) {
    if (opacity_value < 1.0) {
        [<g opacity: util.fmt_num(opacity_value);
            for (c in children) c
        >]
    }
    else { children }
}

// ============================================================
// Driver
// ============================================================
//
// Walks the operator stream once. Returns:
//   { texts: [...SVG-space text elements...],
//     paths: [...PDF-space path elements...] }

pub fn render_page(pdf, page, ops, page_h) {
    let fonts = _resolve_fonts(pdf, page, ops)
    let r = _run_ops(pdf, page, ops, util.IDENTITY, fonts, page_h)
    let texts = [for (t in r.texts) t]
    let paths = [for (p in r.paths) p]
    { texts: texts, paths: paths }
}

pub fn render_page_with_fonts(pdf, page, ops, page_h, fonts) {
    let r = _run_ops(pdf, page, ops, util.IDENTITY, fonts, page_h)
    let texts = [for (t in r.texts) t]
    let paths = [for (p in r.paths) p]
    { texts: texts, paths: paths }
}

// Operator-walk loop, factored out so Form XObject `Do` operators can
// recursively interpret a sub-content-stream with its own CTM and font
// pool. Returns { texts, paths } — caller decides whether to dedupe.
fn _run_ops(pdf, page, ops, init_ctm, fonts, page_h) {
    _run_ops_with_clip_prefix(pdf, page, ops, init_ctm, fonts, page_h, "clip")
}

fn _run_ops_with_clip_prefix(pdf, page, ops, init_ctm, fonts, page_h, clip_prefix) {
    _run_ops_with_state(pdf, page, ops, init_ctm, fonts, page_h, clip_prefix, null)
}

fn _append_all(a, b) {
    a ++ (for (x in b) x)
}

fn _initial_ctx(fonts, init_ctm, inherited_st) {
    _ctx(_initial_run_state(fonts, init_ctm, inherited_st), [], [], [], " ", "nonzero", 0, [],
         " ", " ", 0, 0, 0, [])
}

fn _ctx(st, stack, texts, paths, pending_clip_d, pending_clip_rule, has_pending_clip,
        active_clip_ids, fill_pattern_name, fill_pattern_id, has_fill_pattern,
        fill_pattern_emitted, def_ctr, emitted_pattern_ids) {
    {
        st: st,
        stack: stack,
        texts: texts,
        paths: paths,
        pending_clip_d: pending_clip_d,
        pending_clip_rule: pending_clip_rule,
        has_pending_clip: has_pending_clip,
        active_clip_ids: active_clip_ids,
        fill_pattern_name: fill_pattern_name,
        fill_pattern_id: fill_pattern_id,
        has_fill_pattern: has_fill_pattern,
        fill_pattern_emitted: fill_pattern_emitted,
        def_ctr: def_ctr,
        emitted_pattern_ids: emitted_pattern_ids
    }
}

fn _ctx_with_st(ctx, st) {
    _ctx(st, ctx.stack, ctx.texts, ctx.paths, ctx.pending_clip_d, ctx.pending_clip_rule,
         ctx.has_pending_clip, ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
         ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr, ctx.emitted_pattern_ids)
}

fn _ctx_with_texts(ctx, st, texts) {
    _ctx(st, ctx.stack, texts, ctx.paths, ctx.pending_clip_d, ctx.pending_clip_rule,
         ctx.has_pending_clip, ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
         ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr, ctx.emitted_pattern_ids)
}

fn _ctx_with_paths(ctx, paths) {
    _ctx(ctx.st, ctx.stack, ctx.texts, paths, ctx.pending_clip_d, ctx.pending_clip_rule,
         ctx.has_pending_clip, ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
         ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr, ctx.emitted_pattern_ids)
}

fn _ctx_with_stack(ctx, stack) {
    _ctx(ctx.st, stack, ctx.texts, ctx.paths, ctx.pending_clip_d, ctx.pending_clip_rule,
         ctx.has_pending_clip, ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
         ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr, ctx.emitted_pattern_ids)
}

fn _push_graphics_state(ctx) {
    _ctx_with_stack(ctx, ctx.stack ++ [{
          st: ctx.st,
          pending_clip_d: ctx.pending_clip_d,
          pending_clip_rule: ctx.pending_clip_rule,
          has_pending_clip: ctx.has_pending_clip,
          active_clip_ids: ctx.active_clip_ids,
          fill_pattern_name: ctx.fill_pattern_name,
          fill_pattern_id: ctx.fill_pattern_id,
          has_fill_pattern: ctx.has_fill_pattern,
          fill_pattern_emitted: ctx.fill_pattern_emitted,
          fill_cs: ctx.st.fill_cs,
          stroke_cs: ctx.st.stroke_cs
      }])
}

fn _pop_graphics_state(ctx) {
    let m = len(ctx.stack)
    if (m < 1) { ctx }
    else {
        let saved = ctx.stack[m - 1]
        let restored0 = _restore_state(saved.st)
        let restored1 = _with_fill_cs(restored0, saved.fill_cs)
        let restored2 = _with_stroke_cs(restored1, saved.stroke_cs)
           _ctx(restored2, (for (k, v in ctx.stack where k < (m - 1)) v), ctx.texts, ctx.paths,
               saved.pending_clip_d, saved.pending_clip_rule, saved.has_pending_clip,
               saved.active_clip_ids, saved.fill_pattern_name, saved.fill_pattern_id,
               saved.has_fill_pattern, saved.fill_pattern_emitted, ctx.def_ctr, ctx.emitted_pattern_ids)
    }
}

fn _step_color(ctx, opr, operands, pdf, page, clip_prefix) {
    let pattern_nm = if (opr == "scn" or opr == "SCN") { _pattern_name_from_ops(operands) } else { null }
    if (pattern_nm != null) {
        let pid = clip_prefix ++ "pat" ++ (ctx.def_ctr)
        let pat = { defs: [], fill: "url(#" ++ pid ++ ")" }
        if (opr == "scn") {
            _ctx(_set_pattern_fill(ctx.st, pat), ctx.stack, ctx.texts, ctx.paths,
                 ctx.pending_clip_d, ctx.pending_clip_rule, ctx.has_pending_clip,
                 ctx.active_clip_ids, pattern_nm, pid, 1, 0, ctx.def_ctr + 1,
                 ctx.emitted_pattern_ids)
        }
        else {
            _ctx(_set_pattern_stroke(ctx.st, pat), ctx.stack, ctx.texts, ctx.paths,
                 ctx.pending_clip_d, ctx.pending_clip_rule, ctx.has_pending_clip,
                 ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
                 ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr + 1,
                 ctx.emitted_pattern_ids)
        }
    }
    else {
        let next_st = _apply_color(ctx.st, opr, operands, pdf, page)
        if (opr == "rg" or opr == "g" or opr == "k" or opr == "sc") {
            _ctx(next_st, ctx.stack, ctx.texts, ctx.paths, ctx.pending_clip_d,
                 ctx.pending_clip_rule, ctx.has_pending_clip, ctx.active_clip_ids,
                 ctx.fill_pattern_name, ctx.fill_pattern_id, 0, 0, ctx.def_ctr,
                 ctx.emitted_pattern_ids)
        }
        else { _ctx_with_st(ctx, next_st) }
    }
}

fn _step_pending_clip(ctx, rule) {
    let d = path.current_path_d(ctx.st.path)
    if (d != "" and len(d) > 0) {
        _ctx(ctx.st, ctx.stack, ctx.texts, ctx.paths, d, rule, 1, ctx.active_clip_ids,
             ctx.fill_pattern_name, ctx.fill_pattern_id, ctx.has_fill_pattern,
             ctx.fill_pattern_emitted, ctx.def_ctr, ctx.emitted_pattern_ids)
    }
    else { ctx }
}

fn _step_shading(ctx, operands, pdf, page) {
    let mb = _media_box(page)
    let s = shading.from_sh_op(pdf, page, ctx.st.ctm, operands, mb.w, mb.h, ctx.def_ctr)
    let paths1 = _append_all(ctx.paths, s.defs)
    let paths2 = if (len(s.emit) > 0) { paths1 ++ _wrap_emit_with_ctm(s.emit, ctx.st.ctm, ctx.active_clip_ids) }
                 else { paths1 }
    _ctx(ctx.st, ctx.stack, ctx.texts, paths2, ctx.pending_clip_d, ctx.pending_clip_rule,
         ctx.has_pending_clip, ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
         ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr + 1, ctx.emitted_pattern_ids)
}

fn _partition_defs(paths, i, n, defs, draws) {
    if (i >= n) { { defs: defs, draws: draws } }
    else if (_is_clip_def(paths[i])) { _partition_defs(paths, i + 1, n, defs ++ [paths[i]], draws) }
    else { _partition_defs(paths, i + 1, n, defs, draws ++ [paths[i]]) }
}

fn _append_image_wraps(paths, imgs, k, n, opacity_value, clip_ids) {
    if (k >= n) { paths }
    else {
        let img_emit = _image_opacity_group([imgs[k]], opacity_value)
        _append_image_wraps(paths ++ _wrap_emit_with_ctm(img_emit, util.IDENTITY, clip_ids),
                            imgs, k + 1, n, opacity_value, clip_ids)
    }
}

fn _step_do(ctx, operands, i, pdf, page, fonts, page_h, clip_prefix) {
    let fc = _form_content_for_do(pdf, page, operands)
    let has_form = (fc != null and fc.data != "")
    if (has_form) {
        let form_ops = pdf_parse_content_stream(fc.data)
        let form_ctm = util.matrix_mul(fc.matrix, ctx.st.ctm)
        let form_page = _form_page(page, fc)
        let form_fonts = _resolve_fonts(pdf, form_page, form_ops) ++ fonts
        let sub_prefix = _clip_prefix_for_do(clip_prefix, operands, i)
        let group_opacity = _form_group_opacity(pdf, ctx.st, fc)
        let child_st = _form_child_state(pdf, ctx.st, fc)
        let sub = _run_ops_with_state(pdf, form_page, form_ops, form_ctm, form_fonts, page_h, sub_prefix, child_st)
        let parts = _partition_defs(sub.paths, 0, len(sub.paths), [], [])
        let paths1 = _append_all(ctx.paths, parts.defs)
        let paths2 = if (len(parts.draws) > 0) {
            let grouped = _form_group(parts.draws, group_opacity, _form_bounds_attr(fc))
            paths1 ++ _wrap_emit_with_ctm([grouped], util.IDENTITY, ctx.active_clip_ids)
        }
        else { paths1 }
           _ctx(ctx.st, ctx.stack, _append_all(ctx.texts, sub.texts), paths2,
               ctx.pending_clip_d, ctx.pending_clip_rule, ctx.has_pending_clip,
               ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
               ctx.has_fill_pattern, ctx.fill_pattern_emitted, ctx.def_ctr,
               ctx.emitted_pattern_ids)
    }
    else {
        let imgs = image.apply_do(pdf, page, ctx.st.ctm, operands)
        if (len(imgs) > 0) {
                        _ctx_with_paths(ctx, _append_image_wraps(ctx.paths, imgs, 0, len(imgs), ctx.st.path.fill_opacity, ctx.active_clip_ids))
        }
        else { ctx }
    }
}

fn _clip_element(cid, pending_clip_rule, pending_clip_d, clip_xform) {
    if (pending_clip_rule == "evenodd") {
        <clipPath id: cid;
            <path d: pending_clip_d, transform: clip_xform, 'clip-rule': "evenodd">
        >
    }
    else {
        <clipPath id: cid;
            <path d: pending_clip_d, transform: clip_xform>
        >
    }
}

fn _step_path_emit_pattern(ctx, pdf, page) {
    if (ctx.has_fill_pattern == 1 and ctx.fill_pattern_emitted == 0 and not _list_contains(ctx.emitted_pattern_ids, ctx.fill_pattern_id)) {
        let pat = if (ctx.st.soft_mask != null and ctx.st.soft_mask.kind == "axial_gray") {
            shading.from_pattern_fill_with_alpha(pdf, page, ctx.fill_pattern_name, ctx.fill_pattern_id, ctx.st.ctm, ctx.st.soft_mask)
        }
        else { shading.from_pattern_fill(pdf, page, ctx.fill_pattern_name, ctx.fill_pattern_id, ctx.st.ctm) }
        let st1 = _set_pattern_fill(ctx.st, pat)
           _ctx(st1, ctx.stack, ctx.texts, _append_all(ctx.paths, pat.defs),
               ctx.pending_clip_d, ctx.pending_clip_rule, ctx.has_pending_clip,
               ctx.active_clip_ids, ctx.fill_pattern_name, ctx.fill_pattern_id,
               ctx.has_fill_pattern, 1, ctx.def_ctr,
               ctx.emitted_pattern_ids ++ [ctx.fill_pattern_id])
    }
    else { ctx }
}

fn _path_uses_fill(opr) {
    ((opr == "f") or (opr == "F") or (opr == "f*") or
     (opr == "B") or (opr == "B*") or (opr == "b") or (opr == "b*"))
}

fn _step_path(ctx, opr, operands, pdf, page, clip_prefix) {
    let ctx0 = if (_path_uses_fill(opr)) { _step_path_emit_pattern(ctx, pdf, page) } else { ctx }
    let pr = path.apply_op(ctx0.st.path, opr, operands)
    let st1 = _with_path(ctx0.st, pr.state)
    let did_paint = (len(pr.emit) > 0) or (opr == "n")
    let ctx1 = _ctx_with_st(ctx0, st1)
    let ctx2 = if (did_paint and ctx0.has_pending_clip == 1) {
        let cid = clip_prefix ++ (ctx0.def_ctr)
        let cp = _clip_element(cid, ctx.pending_clip_rule, ctx.pending_clip_d, util.fmt_matrix(st1.ctm))
        _ctx(ctx1.st, ctx1.stack, ctx1.texts, ctx1.paths ++ [cp],
             ctx1.pending_clip_d, ctx1.pending_clip_rule, 0,
             ctx0.active_clip_ids ++ [cid], ctx1.fill_pattern_name, ctx1.fill_pattern_id,
             ctx1.has_fill_pattern, ctx1.fill_pattern_emitted, ctx0.def_ctr + 1,
             ctx1.emitted_pattern_ids)
    }
    else { ctx1 }
    if (len(pr.emit) > 0) {
        let base_emit = _wrap_emit_with_ctm(pr.emit, ctx2.st.ctm, ctx2.active_clip_ids)
        _ctx_with_paths(ctx2, ctx2.paths ++ base_emit)
    }
    else { ctx2 }
}

fn _run_ops_step(ctx, op, i, pdf, page, fonts, page_h, clip_prefix) {
    let opr = op.op
    let operands = op.operands
    if (opr == "q") { _push_graphics_state(ctx) }
    else if (opr == "Q") { _pop_graphics_state(ctx) }
    else if (opr == "cm") { _ctx_with_st(ctx, _op_cm(ctx.st, operands)) }
    else if (opr == "Tf") { _ctx_with_st(ctx, _op_Tf(ctx.st, operands)) }
    else if (_is_text_op(opr)) {
        let tr = text.apply_op(ctx.st.text, ctx.st.ctm, opr, operands, page_h)
        _ctx_with_texts(ctx, _with_text(ctx.st, tr.state), _append_all(ctx.texts, if (tr.emit == null) [] else tr.emit))
    }
    else if (_is_color_op(opr)) { _step_color(ctx, opr, operands, pdf, page, clip_prefix) }
    else if (opr == "gs") { _ctx_with_st(ctx, _apply_gs(ctx.st, operands, pdf, page)) }
    else if (opr == "W") { _step_pending_clip(ctx, "nonzero") }
    else if (opr == "W*") { _step_pending_clip(ctx, "evenodd") }
    else if (opr == "sh") { _step_shading(ctx, operands, pdf, page) }
    else if (_is_noop_op(opr)) { ctx }
    else if (opr == "BI" or opr == "ID" or opr == "EI") { ctx }
    else if (opr == "inline_image") {
        let imgs = image.apply_inline_with_page(pdf, page, ctx.st.ctm, operands)
        if (len(imgs) > 0) {
            let img_emit = _image_opacity_group(imgs, ctx.st.path.fill_opacity)
            _ctx_with_paths(ctx, ctx.paths ++ _wrap_emit_with_ctm(img_emit, ctx.st.ctm, ctx.active_clip_ids))
        }
        else { ctx }
    }
    else if (opr == "Do") { _step_do(ctx, operands, i, pdf, page, fonts, page_h, clip_prefix) }
    else if (path.handles(opr)) { _step_path(ctx, opr, operands, pdf, page, clip_prefix) }
    else { ctx }
}

fn _run_ops_loop(pdf, page, ops, i, n, fonts, page_h, clip_prefix, ctx) {
    if (i >= n) { ctx }
    else {
        let next = _run_ops_step(ctx, ops[i], i, pdf, page, fonts, page_h, clip_prefix)
        _run_ops_loop(pdf, page, ops, i + 1, n, fonts, page_h, clip_prefix, next)
    }
}

fn _run_ops_with_state(pdf, page, ops, init_ctm, fonts, page_h, clip_prefix, inherited_st) {
    let ctx = _run_ops_loop(pdf, page, ops, 0, len(ops), fonts, page_h,
                            clip_prefix, _initial_ctx(fonts, init_ctm, inherited_st))
    { texts: text.dedupe_overprints(ctx.texts), paths: ctx.paths }
}
