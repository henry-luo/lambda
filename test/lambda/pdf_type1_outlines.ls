// Embedded Type 1 painting uses PDF character codes; Unicode remains copy text.
import font: lambda.pdf.font
import text: lambda.pdf.text
import util: lambda.pdf.util
import html: lambda.pdf.html

let pdf = input("test/input/math_intensive_test.pdf", 'pdf')^
let outlined = [for (obj in pdf.objects
    where obj.content.Type == "Font" and obj.content.Subtype == "Type1")
    obj.content.glyph_paths != null];

// A non-identity matrix, spacing and TJ kerning must move the original glyphs.
let fi = {family: "serif", weight: "normal", style: "normal", to_unicode: {'65': "A"},
    encoding: null, widths: [500], first_char: 65, last_char: 65,
    glyph_paths: {'65': "M0 0 L500 0 L250 700 Z"}}
let st = {*: text.new_state(null), in_text: true, font_info: fi, font_size: 10.0,
    tm: [0.0, 2.0, -2.0, 0.0, 30.0, 40.0], char_space: 1.0, hor_scale: 50.0, rise: 3.0}
let run = text.apply_op(st, util.IDENTITY, "TJ", [{kind: "array", value:
    [{kind: "string", value: "A"}, -200, {kind: "hex", value: "41"}]}], 200.0)
let paints = [for (el in run.emit where name(el) == 'g') el]
let copies = [for (el in run.emit where name(el) == 'text') el]
let overlay = html.text_layer(run.emit, 200, 200, null);

[len(outlined) == 21, all(outlined),
 len(paints) == 2, len(copies) == 1, len([for (child in overlay where child is element) child]) == 1,
 paints[0][0].transform == "matrix(0 -0.01 -0.02 0 24 160)",
 paints[1][0].transform == "matrix(0 -0.01 -0.02 0 24 152)",
 run.state.tm[4] == 30.0, run.state.tm[5] == 54.0,
 copies[0]['fill-opacity'] == "0", overlay[0][0] == "AA "]
