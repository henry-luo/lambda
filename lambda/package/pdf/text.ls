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
    parts |> join("")
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
        fill_opacity:   1.0,
        stroke_opacity: 1.0,
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

pub fn set_opacity(st, fa, sa) {
    { *: _base_state(st), fill_opacity: fa, stroke_opacity: sa }
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
        fill_opacity:   st.fill_opacity,
        stroke_opacity: st.stroke_opacity,
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

fn _emit_text(st, ctm, page_h, content, run_advance) {
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
        let raw_font = _raw_font_attr(fi)
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
        let fill_alpha = if (st.fill_opacity < 1.0) st.fill_opacity else 1.0
        let stroke_alpha = if (st.stroke_opacity < 1.0) st.stroke_opacity else 1.0
        let needs_text_alpha = ((svg_fill != "none" and fill_alpha < 1.0) or (needs_stroke and stroke_alpha < 1.0))
        // Tz — horizontal scale, percentage. 100 = no change. Apply via
        // a transform on the <text> origin so glyph widths scale but
        // the baseline stays fixed. Skip when ~100 to keep markup tidy.
        let tr = _text_transform_components(st, ctm, x, y)
        let hs = tr.hscale
        let skew_x = tr.skew_x
        let scaled = tr.scaled
        let text_len = util.fmt_num(run_advance)
        let scaled_text_len = _scaled_text_length(run_advance, hs)
        let text_xform = tr.attr
        if (scaled and needs_stroke and needs_text_alpha) {
            <text x: "0", y: "0",
                  transform: text_xform,
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    scaled_text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill,
                  stroke:        svg_stroke,
                  'fill-opacity': util.fmt_num(fill_alpha),
                  'stroke-opacity': util.fmt_num(stroke_alpha);
                content
            >
        }
        else if (scaled and needs_stroke) {
            <text x: "0", y: "0",
                  transform: text_xform,
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    scaled_text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill,
                  stroke:        svg_stroke;
                content
            >
        }
        else if (scaled and needs_text_alpha) {
            <text x: "0", y: "0",
                  transform: text_xform,
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  textLength:    scaled_text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill,
                  'fill-opacity': util.fmt_num(fill_alpha);
                content
            >
        }
        else if (scaled) {
            <text x: "0", y: "0",
                  transform: text_xform,
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    scaled_text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill;
                content
            >
        }
        else if (needs_stroke and needs_text_alpha) {
            <text x: util.fmt_num(x), y: util.fmt_num(y),
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill,
                  stroke:        svg_stroke,
                  'fill-opacity': util.fmt_num(fill_alpha),
                  'stroke-opacity': util.fmt_num(stroke_alpha);
                content
            >
        }
        else if (needs_stroke) {
            <text x: util.fmt_num(x), y: util.fmt_num(y),
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill,
                  stroke:        svg_stroke;
                content
            >
        }
        else if (needs_text_alpha) {
            <text x: util.fmt_num(x), y: util.fmt_num(y),
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill,
                  'fill-opacity': util.fmt_num(fill_alpha);
                content
            >
        }
        else {
            <text x: util.fmt_num(x), y: util.fmt_num(y),
                  'font-family': family,
                  'font-size':   util.fmt_num(eff_size),
                  'font-weight': weight,
                  'font-style':  style,
                  'data-pdf-raw-font': raw_font,
                  'data-pdf-x': util.fmt_num(x),
                  'data-pdf-y': util.fmt_num(y),
                  'data-pdf-size': util.fmt_num(eff_size),
                  'data-pdf-width': util.fmt_num(run_advance),
                  textLength:    text_len,
                  lengthAdjust:  "spacingAndGlyphs",
                  fill:          svg_fill;
                content
            >
        }
    }
}

fn _raw_font_attr(fi) {
    if (fi and fi.to_unicode == null) { "true" }
    else { "false" }
}

fn _emit_one(st, ctm, page_h, txt, run_advance) {
    let el = _emit_text(st, ctm, page_h, txt, run_advance)
    if (el) { [el] } else { [] }
}

fn _run_advance_between(a, b) {
    let dx = b.tm[4] - a.tm[4]
    if (dx < 0.0) { 0.0 - dx } else { dx }
}

fn _effective_font_size(st, ctm) {
    let scale = _text_y_axis_scale(st, ctm)
    let size = st.font_size * scale
    if (size < 1.0) { 12.0 } else { size }
}

fn _text_x_axis_scale(st, ctm) {
    let a = st.tm[0] * ctm[0] + st.tm[1] * ctm[2]
    let b = st.tm[0] * ctm[1] + st.tm[1] * ctm[3]
    math.sqrt(a * a + b * b)
}

fn _text_y_axis_scale(st, ctm) {
    let a = st.tm[2] * ctm[0] + st.tm[3] * ctm[2]
    let b = st.tm[2] * ctm[1] + st.tm[3] * ctm[3]
    if (util.fabs(b) > 0.001) {
        if (b < 0.0) { 0.0 - b } else { b }
    }
    else { math.sqrt(a * a + b * b) }
}

fn _text_transform_hscale(st, ctm) {
    let ys = _text_y_axis_scale(st, ctm)
    let xs = _text_x_axis_scale(st, ctm)
    let axis_scale = if (ys > 0.0) { xs / ys } else { 1.0 }
    axis_scale * (st.hor_scale / 100.0)
}

fn _text_transform_skew_x(st, ctm) {
    let hx_y = st.tm[0] * ctm[1] + st.tm[1] * ctm[3]
    if (util.fabs(hx_y) > 0.001) { 0.0 }
    else {
        let vx = st.tm[2] * ctm[0] + st.tm[3] * ctm[2]
        let ys = _text_y_axis_scale(st, ctm)
        if (ys > 0.0) { (0.0 - vx) / ys }
        else { 0.0 }
    }
}

fn _text_transform_components(st, ctm, x, y) {
    let ys = _text_y_axis_scale(st, ctm)
    let hx0 = st.tm[0] * ctm[0] + st.tm[1] * ctm[2]
    let hy0 = st.tm[0] * ctm[1] + st.tm[1] * ctm[3]
    let vx0 = st.tm[2] * ctm[0] + st.tm[3] * ctm[2]
    let vy0 = st.tm[2] * ctm[1] + st.tm[3] * ctm[3]
    let hx = if (ys > 0.0) { hx0 / ys } else { 1.0 }
    let hy = if (ys > 0.0) { (0.0 - hy0) / ys } else { 0.0 }
    let vx = if (ys > 0.0) { (0.0 - vx0) / ys } else { 0.0 }
    let vy = if (ys > 0.0) { (0.0 - vy0) / ys } else { 1.0 }
    let rot = util.fabs(hy) > 0.001
    let hscale = _text_transform_hscale(st, ctm)
    let skew_x = _text_transform_skew_x(st, ctm)
    if (rot) {
        let hs = st.hor_scale / 100.0
        { scaled: true,
          hscale: if (hs > 0.0) { hs } else { 1.0 },
          skew_x: 0.0,
          attr: _text_full_transform_attr(hx * hs, hy * hs, vx, vy, x, y) }
    }
    else {
        { scaled: ((util.fabs(hscale - 1.0) > 0.001) or (util.fabs(skew_x) > 0.001)),
          hscale: hscale,
          skew_x: skew_x,
          attr: _text_transform_attr(x, y, hscale, skew_x) }
    }
}

fn _text_full_transform_attr(a, b, c, d, x, y) {
    "matrix(" ++ util.fmt_num(a) ++ " " ++ util.fmt_num(b) ++ " " ++
        util.fmt_num(c) ++ " " ++ util.fmt_num(d) ++ " " ++
        util.fmt_num(x) ++ " " ++ util.fmt_num(y) ++ ")"
}

fn _text_transform_attr(x, y, hs, skew_x) {
    "matrix(" ++ util.fmt_num(hs) ++ " 0 " ++ util.fmt_num(skew_x) ++ " 1 " ++
        util.fmt_num(x) ++ " " ++ util.fmt_num(y) ++ ")"
}

fn _scaled_text_length(run_advance, hs) {
    if (hs > 0.0) { util.fmt_num(run_advance / hs) }
    else { util.fmt_num(run_advance) }
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
    let base = _text_width(st, ctm, txt)
    let spacing = ((st.char_space * float(n)) + (st.word_space * float(_space_count(txt)))) * _text_space_scale(st, ctm)
    (base + spacing) * _text_transform_hscale(st, ctm)
}

fn _text_advance_codes(st, ctm, codes) {
    let n = len(codes)
    let base = _codes_width_units(st.font_info, codes) / 1000.0 * _effective_font_size(st, ctm)
    let words = len(for (c in codes where c == 32) c)
    let spacing = ((st.char_space * float(n)) + (st.word_space * float(words))) * _text_space_scale(st, ctm)
    (base + spacing) * _text_transform_hscale(st, ctm)
}

fn _num_width(v) {
    if (v is int) { float(v) }
    else if (v is float) { v }
    else { 500.0 }
}

fn _cid_width_segment_value(seg, code) {
    if (seg.widths is array and code >= seg.first and code < seg.first + len(seg.widths)) {
        _num_width(seg.widths[code - seg.first])
    }
    else if (seg.width != null and code >= seg.first and code <= seg.last) { _num_width(seg.width) }
    else { null }
}

fn _cid_width_lookup_loop(segments, code, i, n) {
    if (i >= n) { null }
    else {
        let hit = _cid_width_segment_value(segments[i], code)
        if (hit != null) { hit } else { _cid_width_lookup_loop(segments, code, i + 1, n) }
    }
}

fn _cid_width_units(fi, code) {
    if (fi and fi.cid_widths is array) {
        let hit = _cid_width_lookup_loop(fi.cid_widths, code, 0, len(fi.cid_widths))
        if (hit != null) { hit } else { _num_width(fi.default_width) }
    }
    else { null }
}

fn _glyph_width_units(fi, code) {
    let cid_width = _cid_width_units(fi, code)
    if (cid_width != null) { cid_width }
    else if (fi and fi.widths is array and code >= fi.first_char and code <= fi.last_char) {
        let idx = code - fi.first_char
        if (idx >= 0 and idx < len(fi.widths)) { _num_width(fi.widths[idx]) }
        else { 500.0 }
    }
    else { 500.0 }
}

fn _text_width_units(fi, txt) {
    let n = len(txt)
    if (n == 0) { 0.0 }
    else {
        let parts = for (i in 0 to (n - 1)) _glyph_width_units(fi, ord(txt[i]))
        parts |> sum()
    }
}

fn _codes_width_units(fi, codes) {
    if (len(codes) == 0) { 0.0 }
    else {
        let parts = for (c in codes) _glyph_width_units(fi, c)
        parts |> sum()
    }
}

fn _text_width(st, ctm, txt) {
    _text_width_units(st.font_info, txt) / 1000.0 * _effective_font_size(st, ctm)
}

fn _hex_codes_at(hex: string, i: int, cmap) {
    let n = len(hex)
    if (i >= n) { [] }
    else {
        let c8 = if (i + 8 <= n) util.hex_code_at(hex, i, 8) else -1
        if (c8 >= 0 and cmap and cmap[string(c8)]) { [c8] ++ _hex_codes_at(hex, i + 8, cmap) }
        else {
            let c6 = if (i + 6 <= n) util.hex_code_at(hex, i, 6) else -1
            if (c6 >= 0 and cmap and cmap[string(c6)]) { [c6] ++ _hex_codes_at(hex, i + 6, cmap) }
            else {
                let c4 = if (i + 4 <= n) util.hex_code_at(hex, i, 4) else -1
                if (c4 >= 0 and cmap and cmap[string(c4)]) { [c4] ++ _hex_codes_at(hex, i + 4, cmap) }
                else { [util.hex_byte_at(hex, i)] ++ _hex_codes_at(hex, i + 2, cmap) }
            }
        }
    }
}

fn _operand_codes(op, font_info) {
    let cmap = if (font_info) font_info.to_unicode else null
    if (op is map and op.kind == "string") {
        let s = op.value
        if (len(s) == 0) { [] }
        else { for (i in 0 to (len(s) - 1)) ord(s[i]) }
    }
    else if (op is map and op.kind == "hex") { _hex_codes_at(op.value, 0, cmap) }
    else { [] }
}

fn _text_advance_for_operand(st, ctm, op) {
    _text_advance_codes(st, ctm, _operand_codes(op, st.font_info))
}

fn _tj_adjustment(st, ctm, v) {
    (0.0 - util.num(v)) / 1000.0 * _effective_font_size(st, ctm) * (st.hor_scale / 100.0)
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
    let fsize = if (n >= 2) util.num(ops[1]) else st.font_size
    { state: _set_font(st, fname, fsize), emit: null }
}

fn _op_Tm(st, ops) {
    if (len(ops) >= 6) {
        let m = [util.num(ops[0]), util.num(ops[1]), util.num(ops[2]),
             util.num(ops[3]), util.num(ops[4]), util.num(ops[5])]
        { state: _set_tm(st, m), emit: null }
    }
    else { { state: st, emit: null } }
}

fn _op_Td(st, ops) {
    if (len(ops) >= 2) { { state: _move(st, util.num(ops[0]), util.num(ops[1])), emit: null } }
    else               { { state: st, emit: null } }
}

fn _op_TD(st, ops) {
    if (len(ops) >= 2) {
        let tx = util.num(ops[0])
        let ty = util.num(ops[1])
        { state: _move(_set_leading(st, -ty), tx, ty), emit: null }
    }
    else { { state: st, emit: null } }
}

fn _op_Tstar(st) { { state: _move(st, 0.0, -st.leading), emit: null } }

fn _op_TL(st, ops) {
    if (len(ops) >= 1) { { state: _set_leading(st, util.num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Tc <num>: character spacing (additional space inserted between glyphs).
fn _op_Tc(st, ops) {
    if (len(ops) >= 1) { { state: set_char_space(st, util.num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Tw <num>: word spacing (additional space inserted at each 0x20 byte).
fn _op_Tw(st, ops) {
    if (len(ops) >= 1) { { state: set_word_space(st, util.num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Tz <pct>: horizontal scaling (in percent, 100 = identity).
fn _op_Tz(st, ops) {
    if (len(ops) >= 1) { { state: set_hor_scale(st, util.num(ops[0])), emit: null } }
    else               { { state: st, emit: null } }
}

// Ts <num>: text rise (positive shifts baseline up in PDF user space).
fn _op_Ts(st, ops) {
    if (len(ops) >= 1) { { state: set_rise(st, util.num(ops[0])), emit: null } }
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
        let adv = _text_advance_for_operand(st, ctm, ops[0])
        let s1 = if (txt == "") { st } else { _advance_text(st, adv) }
        { state: s1, emit: _emit_one(st, ctm, page_h, txt, adv) }
    }
    else { { state: st, emit: null } }
}

fn _append_emit(out, part) {
    if (part == null) { out }
    else { out ++ (for (p in part) p) }
}

fn _op_TJ_flush(cur, ctm, page_h, seg_text, seg_st, has_seg, emits) {
    if (has_seg) {
        _append_emit(emits, _emit_one(seg_st, ctm, page_h, seg_text, _run_advance_between(seg_st, cur)))
    }
    else { emits }
}

fn _op_TJ_loop(items, i, n, cur, ctm, page_h, seg_text, seg_st, has_seg, emits) {
    if (i >= n) {
        { state: cur, emit: _op_TJ_flush(cur, ctm, page_h, seg_text, seg_st, has_seg, emits) }
    }
    else {
        let it = items[i]
        if (it is map and (it.kind == "string" or it.kind == "hex")) {
            let txt = _tj_text(it, cur.font_info)
            if (txt != "") {
                let next_seg_st = if (has_seg) { seg_st } else { cur }
                let next_cur = _advance_text(cur, _text_advance_for_operand(cur, ctm, it))
                _op_TJ_loop(items, i + 1, n, next_cur, ctm, page_h,
                            seg_text ++ txt, next_seg_st, true, emits)
            }
            else { _op_TJ_loop(items, i + 1, n, cur, ctm, page_h, seg_text, seg_st, has_seg, emits) }
        }
        else if (it is int or it is float) {
            let adj = util.num(it)
            let next_emits = if (adj < -600.0 and has_seg) {
                _append_emit(emits, _emit_one(seg_st, ctm, page_h, seg_text, _run_advance_between(seg_st, cur)))
            }
            else { emits }
            let next_seg_text = if (adj < -600.0 and has_seg) { "" } else { seg_text }
            let next_has_seg = if (adj < -600.0 and has_seg) { false } else { has_seg }
            let next_cur = _advance_text(cur, _tj_adjustment(cur, ctm, adj))
            _op_TJ_loop(items, i + 1, n, next_cur, ctm, page_h,
                        next_seg_text, seg_st, next_has_seg, next_emits)
        }
        else { _op_TJ_loop(items, i + 1, n, cur, ctm, page_h, seg_text, seg_st, has_seg, emits) }
    }
}

fn _op_TJ(st, ctm, ops, page_h) {
    let op0 = if (len(ops) >= 1) { ops[0] } else { null }
    if (op0 is map and op0.kind == "array") {
        let items = op0.value
        _op_TJ_loop(items, 0, len(items), st, ctm, page_h, "", st, false, [])
    }
    else { { state: st, emit: null } }
}

fn _op_quote(st, ctm, ops, page_h) {
    if (len(ops) >= 1) {
        let s1 = _move(st, 0.0, -st.leading)
        let txt = _decode_operand(ops[0], s1.font_info)
        let adv = _text_advance_for_operand(s1, ctm, ops[0])
        let s2 = if (txt == "") { s1 } else { _advance_text(s1, adv) }
        { state: s2, emit: _emit_one(s1, ctm, page_h, txt, adv) }
    }
    else { { state: st, emit: null } }
}

fn _op_dquote(st, ctm, ops, page_h) {
    if (len(ops) >= 3) {
        let s0 = set_char_space(set_word_space(st, util.num(ops[0])), util.num(ops[1]))
        let s1 = _move(s0, 0.0, -s0.leading)
        let txt = _decode_operand(ops[2], s1.font_info)
        let adv = _text_advance_for_operand(s1, ctm, ops[2])
        let s2 = if (txt == "") { s1 } else { _advance_text(s1, adv) }
        { state: s2, emit: _emit_one(s1, ctm, page_h, txt, adv) }
    }
    else { { state: st, emit: null } }
}

// ============================================================
// Top dispatcher
// ============================================================

pub fn apply_op(st, ctm, opr, ops, page_h) {
    let base = _base_state(st)
    if      (opr == "BT") { _op_BT(base) }
    else if (opr == "ET") { _op_ET(base) }
    else if (opr == "Tf") { _op_Tf(base, ops) }
    else if (opr == "Tm") { _op_Tm(base, ops) }
    else if (opr == "Td") { _op_Td(base, ops) }
    else if (opr == "TD") { _op_TD(base, ops) }
    else if (opr == "T*") { _op_Tstar(base) }
    else if (opr == "TL") { _op_TL(base, ops) }
    else if (opr == "Tc") { _op_Tc(base, ops) }
    else if (opr == "Tw") { _op_Tw(base, ops) }
    else if (opr == "Tz") { _op_Tz(base, ops) }
    else if (opr == "Ts") { _op_Ts(base, ops) }
    else if (opr == "Tr") { _op_Tr(base, ops) }
    else if (opr == "Tj") { _op_Tj(base, ctm, ops, page_h) }
    else if (opr == "TJ") { _op_TJ(base, ctm, ops, page_h) }
    else if (opr == "'")  { _op_quote(base, ctm, ops, page_h) }
    else if (opr == "\"") { _op_dquote(base, ctm, ops, page_h) }
    else                  { { state: base, emit: null } }
}

// ============================================================
// Driver — only mutation point
// ============================================================

fn _render_text_ops_loop(ops, i, n, st, out, page_h) {
    if (i >= n) { out }
    else {
        let r = apply_op(st, util.IDENTITY, ops[i].op, ops[i].operands, page_h)
        _render_text_ops_loop(ops, i + 1, n, r.state, _append_emit(out, r.emit), page_h)
    }
}

pub fn render_text_ops(ops, fonts, page_h) {
    _render_text_ops_loop(ops, 0, len(ops), new_state(fonts), [], page_h)
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

fn _dedupe_overprints_loop(texts, i, n, out) {
    if (i >= n) { out }
    else {
        let cur = texts[i]
        let ol = len(out)
        let dup = if (ol > 0) { _is_overprint_dup(out[ol - 1], cur) } else { false }
        let next_out = if (dup) { out } else { out ++ [cur] }
        _dedupe_overprints_loop(texts, i + 1, n, next_out)
    }
}

pub fn dedupe_overprints(texts) {
    _dedupe_overprints_loop(texts, 0, len(texts), [])
}
