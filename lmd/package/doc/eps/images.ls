import obj: .objects
import files: .files
import gfx: .graphics
import matrices: .matrices
import util: lambda.doc.pdf.util

pub let SIGNATURES = {'image': "*", 'colorimage': "*"}

pub pn start_image(var m, op) int^ {
    var components = 1
    var planar = false
    if (op == "colorimage") {
        let args = obj.take_operands(m, 2)^
        let valid = obj.require(m, args[0] is bool and obj.is_integer(args[1]) and obj.number_value(args[1]) in [1, 3, 4], "typecheck: colorimage")^
        planar = args[0]; components = obj.number_value(args[1])
    }
    let planes = if (planar) components else 1
    let args = obj.take_operands(m, 4 + planes)^
    let width = obj.number_value(args[0]); let height = obj.number_value(args[1]); let bits = obj.number_value(args[2])
    let valid = obj.require(m, width is int and height is int and width > 0 and height > 0 and
        width <= m.limits.max_image_pixels / height and bits is int and bits in [1, 2, 4, 8], "rangecheck or limitcheck: image geometry")^
    let matrix = matrices.read_matrix(m, args[3])^
    let transform = util.matrix_mul(gfx.inverse(m, matrix)^, m.graphics.ctm)
    let bytes = int(ceil(float(width * bits * components / planes) / 8.0)) * height
    let bounded = obj.require(m, bytes * planes <= m.limits.max_decoded_bytes, "limitcheck: image bytes")^
    let id = len(m.images)
    push(m.images, {sources: slice(args, 4), data: [for (i in 1 to planes) []], counts: fill(planes, 0),
        bytes: bytes, width: width, height: height, bits: bits, components: components,
        matrix: transform, clips: m.graphics.clips})
    push(m.exec, {kind: "image", id: id, plane: 0, awaiting: false, base: len(m.stack)})
    return 0
}

pn append_bytes(var m, id, plane, data) int^ {
    let amount = min(len(data), m.images[id].bytes - m.images[id].counts[plane])
    let progress = obj.require(m, amount > 0, "ioerror: empty image data source")^
    for (i in 0 to (amount - 1)) { push(m.images[id].data[plane], data[i]) }
    m.images[id].counts[plane] = m.images[id].counts[plane] + amount
    return 0
}

pub pn resume(var m, frame) int^ {
    let id = frame.id
    if (frame.awaiting) {
        let valid = obj.require(m, len(m.stack) == frame.base + 1, "typecheck: image callback stack")^
        let result = obj.take_operands(m, 1)^
        let entry = obj.entry(m, result[0])^
        let string_result = obj.require(m, entry.kind == "string", "typecheck: image data source result")^
        let appended = append_bytes(m, id, frame.plane, obj.values(m, result[0])^)^
    }
    let counts = m.images[id].counts
    let least = min(counts)
    if (least >= m.images[id].bytes) {
        let uri = ps_png(m.images[id].data, {width: m.images[id].width, height: m.images[id].height,
            bits: m.images[id].bits, components: m.images[id].components}, m.limits)
        let valid = obj.require(m, uri is string, "ioerror: image encoding")^
        push(m.draws, {node: <image href: uri, x: "0", y: "0", width: string(m.images[id].width), height: string(m.images[id].height), preserveAspectRatio: "none">,
            matrix: m.images[id].matrix, clips: m.images[id].clips})
        m.images[id] = null
        return 0
    }
    let plane = [for (i in 0 to (len(counts) - 1) where counts[i] == least) i][0]
    let source = m.images[id].sources[plane]
    let entry = obj.entry(m, source)^
    if (entry.kind == "array" and source.executable) {
        push(m.exec, {*: frame, plane: plane, awaiting: true})
        let scheduled = obj.schedule(m, source)^
        return 0
    }
    let data = if (entry.kind == "file") files.read_bytes(m, source, "raw", m.images[id].bytes - counts[plane])^.bytes
        else if (entry.kind == "string") obj.values(m, source)^ else raise obj.failure(m, "typecheck: image source")
    let appended = append_bytes(m, id, plane, data)^
    push(m.exec, {*: frame, awaiting: false})
    return 0
}
