// Baseline-relative SVG geometry. Text and dimensions share the same transform.
import util: .util

pub fn make(body: any, width: any, height: any, depth: any, atom: any = "mord") => {
    body: body, width: width, height: height, depth: depth, type: atom,
    italic: 0.0, accent: width / 2.0, glyph: null, glyph_scale: 1.0
}

pub fn empty(width = 0.0) => make(<g>, width, 0.0, 0.0)

pub fn glyph(g, scale, atom = "mord") {
    let ink = g.ink
    // Unencoded OpenType construction pieces still need their font-local outline.
    let body = if (g.codepoint != null and g.text_family != null)
        <text x: 0, y: 0, 'font-family': g.text_family, 'font-size': g.text_size * scale,
            'font-weight': g.text_weight, 'font-style': g.text_style, chr(g.codepoint)>
        else <path d: g.path, transform: "scale(" ++ string(scale) ++ ")">;
    let bx = make(body,
        g.advance * scale, if (ink != null) max(0.0, 0.0 - ink.top * scale) else 0.0,
        if (ink != null) max(0.0, ink.bottom * scale) else 0.0, atom);
    // Ordinary combining marks have zero advance; their ink locates the attachment.
    let attachment = if (g.accent != null) g.accent
        else if (g.has_math == false and g.advance == 0.0 and ink != null) (ink.left + ink.right) / 2.0
        else g.advance / 2.0;
    {*:bx, italic: (g.italic or 0.0) * scale,
        accent: attachment * scale, glyph: g, glyph_scale: scale}
}

pub fn positioned(bx, x, y) => <g transform: "translate(" ++ string(x) ++ " " ++ string(y) ++ ")", bx.body>

// Geometric stretching is the last resort when the face provides no construction.
pub fn stretched(bx, sx, sy) {
    let result = make(<g transform: "scale(" ++ string(sx) ++ " " ++ string(sy) ++ ")", bx.body>,
        bx.width * sx, bx.height * sy, bx.depth * sy, bx.type);
    {*:result, italic: bx.italic * sx, accent: bx.accent * sx}
}

// Entries are {box, x, y}; positive y moves the baseline downwards.
pub fn compose(entries, width, atom = "mord") {
    let height = max([0.0, *[for (entry in entries) entry.box.height - entry.y]])
    let depth = max([0.0, *[for (entry in entries) entry.box.depth + entry.y]]);
    make(<g for (entry in entries) positioned(entry.box, entry.x, entry.y)>, width, height, depth, atom)
}

pub fn row(boxes, atom = "mord") {
    let entries = [for (i, bx in boxes) {box: bx, x: sum([for (j in 0 to (i - 1)) boxes[j].width]), y: 0.0}]
    compose(entries, sum([for (bx in boxes) bx.width]), atom)
}

pub fn shifted(bx, x, y, width) {
    let result = compose([{box: bx, x: x, y: y}], width, bx.type);
    {*:result, italic: bx.italic, accent: bx.accent + x, glyph: bx.glyph, glyph_scale: bx.glyph_scale}
}

pub fn rule(width, thickness, y) =>
    make(<rect x: 0, y: y, width: width, height: thickness>, width, max(0.0, 0.0 - y), max(0.0, y + thickness))

pub fn emit(bx, units, color, font_size = null, label = null, font_css = "") {
    let height = bx.height + bx.depth
    let style = "width:" ++ util.fmt_em(bx.width / units) ++ ";height:" ++ util.fmt_em(height / units) ++
        ";vertical-align:" ++ util.fmt_em(0.0 - bx.depth / units) ++ ";overflow:visible" ++
        (if (font_size != null) ";font-size:" ++ string(font_size) ++ "px" else "");
    <svg xmlns: "http://www.w3.org/2000/svg", class: "lambda-math", role: "math",
        width: util.fmt_em(bx.width / units), height: util.fmt_em(height / units),
        viewBox: "0 " ++ string(0.0 - bx.height) ++ " " ++ string(max(bx.width, 1.0)) ++ " " ++ string(max(height, 1.0)),
        style: style, fill: (color or "currentColor"),
        if (label != null) <title label> else null
        if (font_css != "") <style font_css> else null
        bx.body>
}
