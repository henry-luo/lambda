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
        font.decode_literal_with_font(op.value, font_info)
    }
    else if (op is map and op.kind == "hex") {
        font.decode_hex_with_font(op.value, font_info)
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
        // Text-rendering mode (Tr). 0=fill (default), 1=stroke,
        // 2=fill+stroke, 3=invisible. Modes 4–7 add glyph outlines
        // to the clipping path; we render them like 0–3 today (clip
        // accumulation is owned by the path module).
        render_mode: 0,
        fill:       "rgb(0,0,0)",
        stroke:     "rgb(0,0,0)",
        fonts:      fonts
    }
}

fn _valid_state(st) {
    st is map and st.tm != null and st.tlm != null and st.font_size != null
}

fn _base_state(st) {
    if (_valid_state(st)) { st } else { new_state(null) }
}

// Public: update the text fill color. Called from interp on rg/g/k
// (PDF non-stroking color ops apply to text rendering as well as
// painting). Uses map spread to avoid touching every `_with` callsite.
pub fn set_fill(st, color_str) {
    { *: _base_state(st), fill: color_str }
}

// Public: update the text stroke color. Called from interp on RG/G/K
// so that text drawn under render-mode 1/2 picks up the active stroke.
pub fn set_stroke(st, color_str) {
    { *: _base_state(st), stroke: color_str }
}

// Public: Tc — character spacing in unscaled text-space units.
pub fn set_char_space(st, v) { { *: _base_state(st), char_space: v } }
// Public: Tw — word spacing in unscaled text-space units.
pub fn set_word_space(st, v) { { *: _base_state(st), word_space: v } }
// Public: Tz — horizontal scaling, expressed as a percentage (100 = 100%).
pub fn set_hor_scale(st, v)  { { *: _base_state(st), hor_scale: v } }
// Public: Ts — text rise in unscaled text-space units (pos = up in PDF).
pub fn set_rise(st, v)       { { *: _base_state(st), rise: v } }
// Public: Tr — text-rendering mode (0–7 per PDF 9.3.6).
pub fn set_render_mode(st, n) { { *: _base_state(st), render_mode: n } }

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
        render_mode: st.render_mode,
        fill:       st.fill,
        stroke:     st.stroke,
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
        if (v is map) { v }
        else {
            let hits = (for (p in fonts where p.name == name) p.info)
            if (len(hits) >= 1) { hits[0] } else { null }
        }
    }
}

fn _set_font(st, name, size) {
    let info = _lookup_font(st.fonts, name)
    _with(st, st.tm, st.tlm, name, size, info, st.leading, st.in_text)
}

// Public: set font with a pre-resolved info record (caller resolves
// via font.resolve_font). Lets interp.ls own all font resolution.
pub fn set_font_info(st, name, size, info) {
    let base = _base_state(st)
    _with(base, base.tm, base.tlm, name, size, info, base.leading, base.in_text)
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

fn _emit_text(st, ctm, page_h, content) {
    if (content == "" or content == null) { null }
    else if (st.render_mode == 3) {
        // mode 3: invisible text — do not emit a paint, but keep state.
        null
    }
    else {
        let fi = st.font_info
        let family = if (fi) fi.family else "sans-serif"
        let weight = if (fi) fi.weight else "normal"
        let style  = if (fi) fi.style  else "normal"
        let fill_color   = if (st.fill) st.fill else "rgb(0,0,0)"
        let stroke_color = if (st.stroke) st.stroke else "rgb(0,0,0)"
        // PDF text-rendering matrix Trm = Tm × CTM. The text origin
        // (Tm[4], Tm[5]) must therefore be transformed by the CTM
        // before we convert to SVG coordinates. Most modern producers
        // emit `1 0 0 -1 0 page_h cm` to flip y, then position with
        // Tm — without applying the CTM here, every glyph lands at
        // the bottom of the page (off-by-flip).
        // Apply text rise (Ts): shifts the baseline up in PDF space
        // by `rise` text-space units (so positive moves up).
        let tx = st.tm[4]
        let ty = st.tm[5] + st.rise
        let px = ctm[0] * tx + ctm[2] * ty + ctm[4]
        let py = ctm[1] * tx + ctm[3] * ty + ctm[5]
        let x = px
        let y = _svg_text_y(st, ctm, page_h, py)
        // Effective font size follows the native C++ PDF view path:
        // Tf_size * sqrt(Trm.a^2 + Trm.b^2). This keeps scaled rotated
        // text matrices from collapsing to the fallback size.
        let eff_size = _effective_font_size(st, ctm)
        // Resolve fill / stroke per Tr (text rendering mode).
        //   0 fill          1 stroke         2 fill+stroke   3 invisible
        //   4 fill+clip     5 stroke+clip    6 f+s+clip      7 add-to-clip
        // Clipping variants (4–7) render the same as 0–3; clip
        // accumulation is owned by path.ls's W/W* (text-as-clip is
        // a Phase 8+ refinement).
        let mode = st.render_mode
        let svg_fill   = if (mode == 1 or mode == 5) "none" else fill_color
        let svg_stroke = if (mode == 1 or mode == 2 or mode == 5 or mode == 6) stroke_color
                         else "none"
        let needs_stroke = (svg_stroke != "none")
        // Tz — horizontal scale, percentage. 100 = no change. Apply via
        // a transform on the <text> origin so glyph widths scale but
        // the baseline stays fixed. Skip when ~100 to keep markup tidy.
        let hs = st.hor_scale / 100.0
        let scaled = (util.fabs(hs - 1.0) > 0.001)
        if (scaled and needs_stroke) {
            <text x: "0", y: "0",
                  transform: "translate(" ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y) ++ ") scale(" ++ util.fmt_num(hs) ++ " 1)",
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  fill:          svg_fill,
                  stroke:        svg_stroke;
                content
            >
        }
        else if (scaled) {
            <text x: "0", y: "0",
                  transform: "translate(" ++ util.fmt_num(x) ++ " " ++ util.fmt_num(y) ++ ") scale(" ++ util.fmt_num(hs) ++ " 1)",
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  fill:          svg_fill;
                content
            >
        }
        else if (needs_stroke) {
            <text x: util.fmt_num(x), y: util.fmt_num(y),
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  fill:          svg_fill,
                  stroke:        svg_stroke;
                content
            >
        }
        else {
            <text x: util.fmt_num(x), y: util.fmt_num(y),
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  fill:          svg_fill;
                content
            >
        }
    }
}

fn _emit_one(st, ctm, page_h, txt) {
    let el = _emit_text(st, ctm, page_h, txt)
    if (el) { [el] } else { [] }
}

fn _effective_font_size(st, ctm) {
    let a = st.tm[0] * ctm[0] + st.tm[1] * ctm[2]
    let b = st.tm[0] * ctm[1] + st.tm[1] * ctm[3]
    let scale = math.sqrt(a * a + b * b)
    let size = st.font_size * scale
    if (size < 1.0) { 12.0 } else { size }
}

fn _svg_text_y(st, ctm, page_h, py) {
    if (st.tm[3] < 0.0 and ctm[3] >= 0.0) { py }
    else { float(page_h) - py }
}

fn _advance_text(st, dx) {
    let m = [st.tm[0], st.tm[1], st.tm[2], st.tm[3], st.tm[4] + dx, st.tm[5]]
    _with(st, m, st.tlm, st.font_name, st.font_size, st.font_info, st.leading, st.in_text)
}

fn _space_count(txt) {
    len(for (i in 0 to (len(txt) - 1) where txt[i] == " ") i)
}

fn _text_space_scale(st, ctm) {
    let fs = if (st.font_size == 0.0) { 1.0 } else { st.font_size }
    _effective_font_size(st, ctm) / fs
}

fn _text_advance(st, ctm, txt) {
    let n = len(txt)
    let base = float(n) * _effective_font_size(st, ctm) * 0.5
    let spacing = ((st.char_space * float(n)) + (st.word_space * float(_space_count(txt)))) * _text_space_scale(st, ctm)
    (base + spacing) * (st.hor_scale / 100.0)
}

fn _tj_adjustment(st, ctm, v) {
    (0.0 - _num(v)) / 1000.0 * _effective_font_size(st, ctm) * (st.hor_scale / 100.0)
}

fn _tj_text(op, font_info) {
    if (op is map and (op.kind == "string" or op.kind == "hex")) { _decode_operand(op, font_info) }
    else { "" }
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

// Tc <num>: character spacing (additional space inserted between glyphs).
fn _op_Tc(st, ops) {
    if (len(ops) >= 1) { { state: set_char_space(st, _num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Tw <num>: word spacing (additional space inserted at each 0x20 byte).
fn _op_Tw(st, ops) {
    if (len(ops) >= 1) { { state: set_word_space(st, _num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Tz <pct>: horizontal scaling (in percent, 100 = identity).
fn _op_Tz(st, ops) {
    if (len(ops) >= 1) { { state: set_hor_scale(st, _num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Ts <num>: text rise (positive shifts baseline up in PDF user space).
fn _op_Ts(st, ops) {
    if (len(ops) >= 1) { { state: set_rise(st, _num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Tr <int>: text-rendering mode 0..7.
fn _op_Tr(st, ops) {
    if (len(ops) >= 1) {
        let v = ops[0]
        let m = if (v is int) v else if (v is float) int(v) else 0
        { state: set_render_mode(st, m), emit: null }
    }
    else { { state: st, emit: null } }
}

fn _op_Tj(st, ctm, ops, page_h) {
    if (len(ops) >= 1) {
        let txt = _decode_operand(ops[0], st.font_info)
        let s1 = if (txt == "") { st } else { _advance_text(st, _text_advance(st, ctm, txt)) }
        { state: s1, emit: _emit_one(st, ctm, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

pn _op_TJ(st, ctm, ops, page_h) {
    var op0 = null
    if (len(ops) >= 1) { op0 = ops[0] }
    if (op0 is map and op0.kind == "array") {
        var cur = st
        var seg_text = ""
        var seg_st = st
        var has_seg = false
        var emits = []
        var i = 0
        let items = op0.value
        let n = len(items)
        while (i < n) {
            let it = items[i]
            if (it is map and (it.kind == "string" or it.kind == "hex")) {
                let txt = _tj_text(it, cur.font_info)
                if (txt != "") {
                    if (not has_seg) {
                        seg_st = cur
                        has_seg = true
                    }
                    seg_text = seg_text ++ txt
                    cur = _advance_text(cur, _text_advance(cur, ctm, txt))
                }
            }
            else if (it is int or it is float) {
                let adj = _num(it)
                if (adj < -600.0 and has_seg) {
                    let part = _emit_one(seg_st, ctm, page_h, seg_text)
                    var k = 0
                    let m = len(part)
                    while (k < m) {
                        emits = emits ++ [part[k]]
                        k = k + 1
                    }
                    seg_text = ""
                    has_seg = false
                }
                cur = _advance_text(cur, _tj_adjustment(cur, ctm, adj))
            }
            i = i + 1
        }
        if (has_seg) {
            let part = _emit_one(seg_st, ctm, page_h, seg_text)
            var k = 0
            let m = len(part)
            while (k < m) {
                emits = emits ++ [part[k]]
                k = k + 1
            }
        }
        return { state: cur, emit: emits }
    }
    else { return { state: st, emit: null } }
}

fn _op_quote(st, ctm, ops, page_h) {
    if (len(ops) >= 1) {
        let s1 = _move(st, 0.0, -st.leading)
        let txt = _decode_operand(ops[0], s1.font_info)
        let s2 = if (txt == "") { s1 } else { _advance_text(s1, _text_advance(s1, ctm, txt)) }
        { state: s2, emit: _emit_one(s1, ctm, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

fn _op_dquote(st, ctm, ops, page_h) {
    if (len(ops) >= 3) {
        let s0 = set_char_space(set_word_space(st, _num(ops[0])), _num(ops[1]))
        let s1 = _move(s0, 0.0, -s0.leading)
        let txt = _decode_operand(ops[2], s1.font_info)
        let s2 = if (txt == "") { s1 } else { _advance_text(s1, _text_advance(s1, ctm, txt)) }
        { state: s2, emit: _emit_one(s1, ctm, page_h, txt) }
    }
    else { { state: st, emit: null } }
}

// ============================================================
// Top dispatcher
// ============================================================

pub pn apply_op(st, ctm, opr, ops, page_h) {
    let base = _base_state(st)
    if      (opr == "BT") { return _op_BT(base) }
    else if (opr == "ET") { return _op_ET(base) }
    else if (opr == "Tf") { return _op_Tf(base, ops) }
    else if (opr == "Tm") { return _op_Tm(base, ops) }
    else if (opr == "Td") { return _op_Td(base, ops) }
    else if (opr == "TD") { return _op_TD(base, ops) }
    else if (opr == "T*") { return _op_Tstar(base) }
    else if (opr == "TL") { return _op_TL(base, ops) }
    else if (opr == "Tc") { return _op_Tc(base, ops) }
    else if (opr == "Tw") { return _op_Tw(base, ops) }
    else if (opr == "Tz") { return _op_Tz(base, ops) }
    else if (opr == "Ts") { return _op_Ts(base, ops) }
    else if (opr == "Tr") { return _op_Tr(base, ops) }
    else if (opr == "Tj") { return _op_Tj(base, ctm, ops, page_h) }
    else if (opr == "TJ") { return _op_TJ(base, ctm, ops, page_h) }
    else if (opr == "'")  { return _op_quote(base, ctm, ops, page_h) }
    else if (opr == "\"") { return _op_dquote(base, ctm, ops, page_h) }
    else                  { return { state: base, emit: null } }
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
        let r = apply_op(st, util.IDENTITY, ops[i].op, ops[i].operands, page_h)
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

// ============================================================
// Faux-bold / overprint deduplication
// ============================================================
//
// Some PDFs implement faux-bold by emitting the same glyph twice with a
// tiny x offset (~0.5–1.0 user-space units). When we render with the real
// font (which has correct metrics) these end up as overlapping doubled
// letters. Detect consecutive <text> elements with identical content and
// styling whose x positions differ by < 1.5 units, and drop the duplicate.

fn _attr_str(el, key) {
    let v = el[key]
    if (v == null) "" else string(v)
}

fn _content_str(el) {
    if (len(el) == 0) ""
    else string(el[0])
}

fn _is_overprint_dup(prev, cur) {
    let pt = _attr_str(prev, "transform")
    let ct = _attr_str(cur, "transform")
    if (pt != "" or ct != "") { false }
    else {
        let px = _attr_str(prev, "x")
        let cx = _attr_str(cur, "x")
        if (px == "" or cx == "") { false }
        else if (_attr_str(prev, "y") != _attr_str(cur, "y")) { false }
        else if (_attr_str(prev, "font-family") != _attr_str(cur, "font-family")) { false }
        else if (_attr_str(prev, "font-size")   != _attr_str(cur, "font-size"))   { false }
        else if (_attr_str(prev, "font-weight") != _attr_str(cur, "font-weight")) { false }
        else if (_attr_str(prev, "font-style")  != _attr_str(cur, "font-style"))  { false }
        else if (_attr_str(prev, "fill")        != _attr_str(cur, "fill"))        { false }
        else if (_attr_str(prev, "stroke")      != _attr_str(cur, "stroke"))      { false }
        else if (_content_str(prev) != _content_str(cur))                         { false }
        else {
            let dx = float(cx) - float(px)
            let adx = if (dx < 0.0) (0.0 - dx) else dx
            (adx < 1.5)
        }
    }
}

pub pn dedupe_overprints(texts) {
    var out = []
    var i = 0
    let n = len(texts)
    while (i < n) {
        let cur = texts[i]
        var dup = false
        let ol = len(out)
        if (ol > 0) {
            let prev = out[ol - 1]
            dup = _is_overprint_dup(prev, cur)
        }
        if (not dup) { out = out ++ [cur] }
        i = i + 1
    }
    return out
}
