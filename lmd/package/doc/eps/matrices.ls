import obj: .objects
import gfx: .graphics
import util: lambda.doc.pdf.util

pub let SIGNATURES = {'matrix': "", 'currentmatrix': "a", 'defaultmatrix': "a", 'setmatrix': "a", 'concat': "a",
    'concatmatrix': "aaa", 'invertmatrix': "aa", 'translate': "*", 'scale': "*", 'rotate': "*",
    'transform': "*", 'dtransform': "*", 'itransform': "*", 'idtransform': "*"}

pub fn read_matrix(m, composite) array^ {
    let e = obj.entry(m, composite)^
    let data = obj.values(m, composite)^
    let valid = obj.require(m, e.kind == "array" and len(data) == 6 and all([for (v in data) obj.is_number(v)]), "typecheck: matrix")^;
    [for (v in data) obj.number_value(v)]
}

pub pn write_matrix(var m, composite, matrix) int^ {
    let checked = read_matrix(m, composite)^
    for (i in 0 to 5) { let stored = obj.put_value(m, composite, i, matrix[i])^ }
    return 0
}

pub pn dispatch(var m, op, supplied) int^ {
    if (op == "matrix") {
        let matrix = obj.allocate(m, "array", util.IDENTITY)^
        obj.push_operand(m, matrix)
        return 0
    }
    if (op == "currentmatrix" or op == "defaultmatrix") {
        let stored = write_matrix(m, supplied[0], if (op == "currentmatrix") m.graphics.ctm else util.IDENTITY)^
        obj.push_operand(m, supplied[0])
        return 0
    }
    if (op == "setmatrix" or op == "concat") {
        let matrix = read_matrix(m, supplied[0])^
        m.graphics.ctm = if (op == "setmatrix") matrix else util.matrix_mul(matrix, m.graphics.ctm)
        return 0
    }
    if (op == "concatmatrix" or op == "invertmatrix") {
        let first = read_matrix(m, supplied[0])^
        let result = if (op == "invertmatrix") gfx.inverse(m, first)^
            else util.matrix_mul(first, read_matrix(m, supplied[1])^)
        let dest = supplied[len(supplied) - 1]
        let stored = write_matrix(m, dest, result)^
        obj.push_operand(m, dest)
        return 0
    }
    let checked = obj.require(m, len(m.stack) > 0, "stackunderflow")^
    let destination = m.stack[len(m.stack) - 1]
    let explicit = obj.is_ref(destination)
    let n = if (op == "rotate") 1 else 2
    let raw = obj.take_operands(m, n + if (explicit) 1 else 0)^
    let args = [for (v in raw) obj.number_value(v)]
    let valid = obj.require(m, all([for (i in 0 to (n - 1)) obj.is_number(args[i])]), "typecheck: transform")^
    if (op == "translate" or op == "scale" or op == "rotate") {
        let angle = args[0] * math.pi / 180.0
        let matrix = if (op == "translate") [1.0, 0.0, 0.0, 1.0, args[0], args[1]]
            else if (op == "scale") [args[0], 0.0, 0.0, args[1], 0.0, 0.0]
            else [math.cos(angle), math.sin(angle), -math.sin(angle), math.cos(angle), 0.0, 0.0]
        if (explicit) {
            let stored = write_matrix(m, destination, matrix)^
            obj.push_operand(m, destination)
            return 0
        }
        m.graphics.ctm = util.matrix_mul(matrix, m.graphics.ctm)
        return 0
    }
    let matrix = if (explicit) read_matrix(m, destination)^ else m.graphics.ctm
    let active = if (op == "itransform" or op == "idtransform") gfx.inverse(m, matrix)^ else matrix
    let linear = if (op == "dtransform" or op == "idtransform") [active[0], active[1], active[2], active[3], 0.0, 0.0] else active
    let p = gfx.point(linear, args[0], args[1])
    obj.push_operand(m, p[0]); obj.push_operand(m, p[1])
    return 0
}
