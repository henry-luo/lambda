import obj: .objects
import matrices: .matrices
import pdf_font: lambda.doc.pdf.font
import util: lambda.doc.pdf.util

pub let SIGNATURES = {'findfont': "x", 'scalefont': "dn", 'makefont': "da",
    'setfont': "d", 'currentfont': "", 'selectfont': "xx"}

fn finite_matrix(matrix) => all([for (v in matrix)
    obj.is_number(v) and not (v is nan) and not (v is inf) and v != -inf])

pn frozen(var m, kind, data) map^ {
    let value = obj.allocate(m, kind, data)^
    m.heap[value.id].access = "readonly"
    return value
}

pub pn lookup_font(var m, key) map^ {
    let font_name = obj.key_name(m, key)^
    let info = pdf_font.postscript_basefont(font_name)
    let supported = obj.require(m, info is map, "invalidfont: unavailable font " ++ font_name)^
    if (font_name at m.fonts) { return m.fonts[font_name] }
    // FID is an explicit guest object, so VM restore can reject discarded font identities.
    let identity = frozen(m, "font", {name: font_name, *: info})^
    let matrix = frozen(m, "array", [0.001, 0.0, 0.0, 0.001, 0.0, 0.0])^
    let dictionary = frozen(m, "dict", {FontType: 1, FontName: obj.literal_name(font_name),
        FontMatrix: matrix, FID: identity})^
    m.fonts[font_name] = dictionary
    return dictionary
}

pub fn descriptor(m, value) map^ {
    let entry = obj.entry(m, value)^
    let is_dictionary = obj.require(m, entry.kind == "dict", "invalidfont: expected a font dictionary")^
    let data = obj.values(m, value)^
    let has_identity = obj.require(m, obj.number_value(data.FontType) == 1 and obj.is_ref(data.FID), "invalidfont: missing FID or unsupported FontType")^
    let identity = obj.entry(m, data.FID)^
    let valid_identity = obj.require(m, identity.kind == "font", "invalidfont: invalid FID")^
    let matrix = matrices.read_matrix(m, data.FontMatrix)^
    let valid_matrix = obj.require(m, finite_matrix(matrix), "invalidfont: non-finite FontMatrix")^;
    {dictionary: data, matrix: matrix, resource: obj.values(m, data.FID)^}
}

pn derive(var m, value, matrix) map^ {
    let font = descriptor(m, value)^
    let transformed = util.matrix_mul(font.matrix, matrix)
    let valid = obj.require(m, finite_matrix(transformed), "invalidfont: non-finite font transform")^
    let font_matrix = frozen(m, "array", transformed)^
    // PLRM makefont shares subsidiary objects and replaces only the transformed matrix.
    return frozen(m, "dict", {*: font.dictionary, FontMatrix: font_matrix})^
}

pub pn dispatch(var m, op, args) int^ {
    if (op == "findfont") { obj.push_operand(m, lookup_font(m, args[0])^); return 0 }
    if (op == "currentfont") {
        if (m.graphics.font == null) { m.graphics.font = lookup_font(m, obj.literal_name("Courier"))^ }
        obj.push_operand(m, m.graphics.font)
        return 0
    }
    if (op == "setfont") {
        let checked = descriptor(m, args[0])^
        m.graphics.font = args[0]
        return 0
    }
    let base = if (op == "selectfont") lookup_font(m, args[0])^ else args[0]
    let transform = obj.number_value(args[1])
    let valid = obj.require(m, obj.is_number(transform) or (op != "scalefont" and obj.is_ref(transform)), "typecheck: font transform")^
    let matrix = if (obj.is_number(transform)) [transform, 0.0, 0.0, transform, 0.0, 0.0]
        else matrices.read_matrix(m, transform)^
    let derived = derive(m, base, matrix)^
    if (op == "selectfont") { m.graphics.font = derived; return 0 }
    obj.push_operand(m, derived)
    return 0
}
