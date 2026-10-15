import radiant
import obj: .objects
import fonts: .fonts
import gfx: .graphics
import svg: lambda.doc.pdf.svg
import util: lambda.doc.pdf.util

pub let SIGNATURES = {'show': "s", 'stringwidth': "s", 'ashow': "nns", 'widthshow': "nnis", 'awidthshow': "nninns"}

fn character(m, byte) string^ {
    let valid = obj.require(m, byte >= 32 and byte <= 126, "rangecheck: unsupported StandardEncoding byte")^
    chr(if (byte == 39) 8217 else if (byte == 96) 8216 else byte)
}

pub pn dispatch(var m, op, args) int^ {
    if (m.graphics.font == null) { m.graphics.font = fonts.lookup_font(m, obj.literal_name("Courier"))^ }
    let font = fonts.descriptor(m, m.graphics.font)^
    let bytes = obj.values(m, args[len(args) - 1])^
    let glyphs = [for (byte in bytes) character(m, byte)^]
    let prefix = string(font.dictionary.FID.id) ++ ":"
    let missing = unique([for (byte in bytes where not (prefix ++ string(byte) at m.text_metrics)) byte])
    if (len(missing) > 0) {
        let metrics = radiant.measure_text([for (byte in missing)
            {text: character(m, byte)^, font: {font_family: font.resource.family, font_size: 1000,
                font_weight: if (font.resource.weight == "bold") 700 else 400, font_style: font.resource.style}}], null)
        let measured = obj.require(m, metrics is array and len(metrics) == len(missing), "invalidfont: glyph measurement failed")^
        for (i in 0 to (len(missing) - 1)) { m.text_metrics[prefix ++ string(missing[i])] = metrics[i].width }
    }
    var positions = []
    var advance = 0.0
    var dx = 0.0
    var dy = 0.0
    let extra = if (op == "ashow") [args[0], args[1]] else if (op == "awidthshow") [args[3], args[4]] else [0.0, 0.0]
    for (i in 0 to (len(bytes) - 1)) {
        push(positions, {x: advance, dx: dx, dy: dy})
        advance = advance + m.text_metrics[prefix ++ string(bytes[i])]
        dx = dx + extra[0]; dy = dy + extra[1]
        if ((op == "widthshow" or op == "awidthshow") and bytes[i] == args[2]) {
            dx = dx + args[0]; dy = dy + args[1]
        }
    }
    let matrix = font.matrix
    let width = [matrix[0] * advance + dx, matrix[1] * advance + dy]
    if (op == "stringwidth") { obj.push_operand(m, width[0]); obj.push_operand(m, width[1]); return 0 }
    let origin = gfx.current_point(m)^
    if (len(glyphs) > 0) {
        // explicit per-glyph origins prevent SVG kerning/ligatures from changing PostScript advances.
        let nodes = [for (i in 0 to (len(glyphs) - 1))
            svg.text_run(util.fmt_matrix([matrix[0], matrix[1], -matrix[2], -matrix[3],
                origin[0] + matrix[4] + matrix[0] * positions[i].x + positions[i].dx,
                origin[1] + matrix[5] + matrix[1] * positions[i].x + positions[i].dy]),
                font.resource.family, 1000, m.graphics.path.fill_color, glyphs[i],
                {'font-weight': font.resource.weight, 'font-style': font.resource.style,
                 'xml:space': "preserve", style: "font-kerning:none;font-variant-ligatures:none"})]
        push(m.draws, {node: <g for (node in nodes) node>, matrix: m.graphics.ctm, clips: m.graphics.clips})
    }
    let moved = gfx.build_path(m, "moveto", [origin[0] + width[0], origin[1] + width[1]])^
    return 0
}
