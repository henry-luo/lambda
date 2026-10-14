import obj: .objects
import util: lambda.pdf.util
import path: lambda.pdf.path
import color: lambda.pdf.color

pub let SIGNATURES = {
    'newpath': "", 'gsave': "", 'grestore': "", 'grestoreall': "", 'initmatrix': "",
    'currentpoint': "", 'stroke': "", 'fill': "", 'eofill': "", 'clip': "", 'eoclip': "",
    'moveto': "nn", 'rmoveto': "nn", 'lineto': "nn", 'rlineto': "nn", 'curveto': "nnnnnn", 'rcurveto': "nnnnnn", 'closepath': "",
    'arc': "nnnnn", 'arcn': "nnnnn", 'setgray': "n", 'setrgbcolor': "nnn", 'setcmykcolor': "nnnn",
    'setlinewidth': "n", 'setlinecap': "i", 'setlinejoin': "i", 'setmiterlimit': "n", 'setdash': "an",
    'currentlinewidth': "", 'currentlinecap': "", 'currentlinejoin': "", 'currentmiterlimit': "", 'currentdash': "",
    'rectfill': "nnnn", 'rectstroke': "nnnn", 'rectclip': "nnnn"
}

pub fn initial() => {ctm: util.IDENTITY, path: path.new_state(), current: false, clips: [], space: "gray", components: [0.0], font: null}
pub fn point(matrix, x, y) => [matrix[0] * x + matrix[2] * y + matrix[4], matrix[1] * x + matrix[3] * y + matrix[5]]

pub fn inverse(m, matrix) array^ {
    let det = matrix[0] * matrix[3] - matrix[1] * matrix[2]
    if (det == 0.0) raise obj.failure(m, "undefinedresult: singular matrix")
    else [matrix[3] / det, -matrix[1] / det, -matrix[2] / det, matrix[0] / det,
          (matrix[2] * matrix[5] - matrix[3] * matrix[4]) / det,
          (matrix[1] * matrix[4] - matrix[0] * matrix[5]) / det]
}

pub fn current_point(m) array^ {
    let checked = obj.require(m, m.graphics.current, "nocurrentpoint")^
    point(inverse(m, m.graphics.ctm)^, m.graphics.path.current_x, m.graphics.path.current_y)
}

fn segment(s, matrix) {
    if (s.cmd == "M" or s.cmd == "L") {
        let p = point(matrix, s.x, s.y)
        {cmd: s.cmd, x: p[0], y: p[1]}
    } else if (s.cmd == "C") {
        let a = point(matrix, s.x1, s.y1)
        let b = point(matrix, s.x2, s.y2)
        let c = point(matrix, s.x3, s.y3)
        {cmd: "C", x1: a[0], y1: a[1], x2: b[0], y2: b[1], x3: c[0], y3: c[1]}
    } else s
}

pub pn build_path(var m, op, args) int^ {
    var numbers = args
    var command = op
    if (op == "rmoveto" or op == "rlineto" or op == "rcurveto") {
        let origin = current_point(m)^
        numbers = [for (i in 0 to (len(args) - 1)) args[i] + origin[i % 2]]
    }
    if (op == "moveto" or op == "rmoveto") { command = "m" }
    else if (op == "lineto" or op == "rlineto") { command = "l" }
    else if (op == "curveto" or op == "rcurveto") { command = "c" }
    else if (op == "closepath") { command = "h" }
    if (command != "m") {
        let checked = obj.require(m, m.graphics.current, "nocurrentpoint")^
    }
    let device = [for (i in 0 to (len(numbers) - 1) where i % 2 == 0)
        for (v in point(m.graphics.ctm, numbers[i], numbers[i + 1])) v]
    let result = path.apply_op(m.graphics.path, command, device)
    m.graphics.path = result.state
    m.graphics.current = true
    return 0
}

pub pn paint(var m, op) int^ {
    let p = m.graphics.path
    if (path.has_segments(p)) {
        // path points are frozen at construction; stroke metrics use the paint-time CTM.
        let inv = inverse(m, m.graphics.ctm)^
        let user_path = {*: p, segments: [for (s in p.segments) segment(s, inv)]}
        let result = path.paint(user_path, if (op == "stroke") "none" else p.fill_color,
            if (op == "stroke") p.stroke_color else "none", if (op == "eofill") "evenodd" else "nonzero")
        for (node in result.emit) { push(m.draws, {node: node, matrix: m.graphics.ctm, clips: m.graphics.clips}) }
    }
    m.graphics.path = path.clear_current_path(p)
    m.graphics.current = false
    return 0
}

pub pn push_clip(var m, evenodd = false) int^ {
    let allowed = obj.require(m, len(m.graphics.clips) < m.limits.max_clip_depth, "limitcheck: clip depth")^
    let id = len(m.definitions)
    push(m.definitions, {kind: "clip", id: id, d: path.current_path_d(m.graphics.path),
        rule: if (evenodd) "evenodd" else "nonzero"})
    push(m.graphics.clips, id)
    return 0
}

pub pn arc(var m, args, clockwise) int^ {
    let radius = args[2]
    let checked = obj.require(m, radius >= 0, "rangecheck: arc radius")^
    let raw = args[4] - args[3]
    let sweep = if (clockwise) {
        if (raw <= -360.0) -360.0 else raw - ceil(raw / 360.0) * 360.0
    } else {
        if (raw >= 360.0) 360.0 else raw - floor(raw / 360.0) * 360.0
    }
    let pieces = max(1, int(ceil(abs(sweep) / 90.0)))
    let delta = sweep * math.pi / (180.0 * pieces)
    let first = args[3] * math.pi / 180.0
    let arc_start = [args[0] + radius * math.cos(first), args[1] + radius * math.sin(first)]
    let moved = build_path(m, if (m.graphics.current) "lineto" else "moveto", arc_start)^
    for (i in 0 to (pieces - 1)) {
        let a = first + i * delta
        let b = a + delta
        let k = 4.0 / 3.0 * math.tan(delta / 4.0)
        let curve = [args[0] + radius * (math.cos(a) - k * math.sin(a)),
            args[1] + radius * (math.sin(a) + k * math.cos(a)),
            args[0] + radius * (math.cos(b) + k * math.sin(b)),
            args[1] + radius * (math.sin(b) - k * math.cos(b)),
            args[0] + radius * math.cos(b), args[1] + radius * math.sin(b)]
        let built = build_path(m, "curveto", curve)^
    }
    return 0
}

pub pn dispatch(var m, op, args) int^ {
    if (op == "newpath") { m.graphics.path = path.clear_current_path(m.graphics.path); m.graphics.current = false; return 0 }
    if (op == "gsave") { push(m.gstack, m.graphics); return 0 }
    if (op == "grestore") {
        if (len(m.gstack) > 0) {
            m.graphics = m.gstack[len(m.gstack) - 1]
            m.gstack = slice(m.gstack, 0, len(m.gstack) - 1)
        }
        return 0
    }
    if (op == "grestoreall") {
        if (len(m.gstack) > 0) { m.graphics = m.gstack[0] }
        m.gstack = []
        return 0
    }
    if (op == "initmatrix") { m.graphics.ctm = util.IDENTITY; return 0 }
    if (op == "currentpoint") {
        let p = current_point(m)^
        obj.push_operand(m, p[0]); obj.push_operand(m, p[1])
        return 0
    }
    if (op == "stroke" or op == "fill" or op == "eofill") { let painted = paint(m, op)^; return 0 }
    if (op == "clip" or op == "eoclip") { let emitted = push_clip(m, op == "eoclip")^; return 0 }
    if (op == "arc" or op == "arcn") { let drawn = arc(m, args, op == "arcn")^; return 0 }
    if (op == "moveto" or op == "lineto" or op == "curveto" or op == "closepath" or
        op == "rmoveto" or op == "rlineto" or op == "rcurveto") { let built = build_path(m, op, args)^; return 0 }
    if (op == "setgray" or op == "setrgbcolor" or op == "setcmykcolor") {
        let components = [for (v in args) max(0.0, min(1.0, float(v)))]
        let c = if (op == "setgray") color.gray(components[0])
            else if (op == "setrgbcolor") color.rgb(components[0], components[1], components[2])
            else color.cmyk(components[0], components[1], components[2], components[3])
        m.graphics.path = path.set_stroke_color(path.set_fill_color(m.graphics.path, c), c)
        m.graphics.space = if (op == "setgray") "gray" else if (op == "setrgbcolor") "rgb" else "cmyk"
        m.graphics.components = components
        return 0
    }
    if (op == "setlinewidth") {
        let checked = obj.require(m, args[0] >= 0, "rangecheck: linewidth")^
        m.graphics.path = path.set_line_width(m.graphics.path, args[0])
        return 0
    }
    if (op == "setlinecap" or op == "setlinejoin") {
        let checked = obj.require(m, args[0] >= 0 and args[0] <= 2, "rangecheck: line cap/join")^
        m.graphics.path = if (op == "setlinecap") path.set_line_cap(m.graphics.path, args[0]) else path.set_line_join(m.graphics.path, args[0])
        return 0
    }
    if (op == "setmiterlimit") {
        let checked = obj.require(m, args[0] >= 1, "rangecheck: miter limit")^
        m.graphics.path = path.set_miter_limit(m.graphics.path, args[0])
        return 0
    }
    if (op == "setdash") {
        let dash = [for (v in obj.values(m, args[0])^) obj.number_value(v)]
        let checked = obj.require(m, all([for (n in dash) obj.is_number(n) and n >= 0]) and
            (len(dash) == 0 or sum(dash) > 0), "rangecheck: dash array")^
        m.graphics.path = path.set_dash(m.graphics.path, dash, args[1])
        return 0
    }
    if (op == "currentlinewidth") { obj.push_operand(m, m.graphics.path.line_width); return 0 }
    if (op == "currentlinecap") { obj.push_operand(m, m.graphics.path.line_cap); return 0 }
    if (op == "currentlinejoin") { obj.push_operand(m, m.graphics.path.line_join); return 0 }
    if (op == "currentmiterlimit") { obj.push_operand(m, m.graphics.path.miter_limit); return 0 }
    if (op == "currentdash") {
        obj.push_operand(m, obj.allocate(m, "array", m.graphics.path.dash_array)^)
        obj.push_operand(m, m.graphics.path.dash_phase)
        return 0
    }
    if (op == "rectfill" or op == "rectstroke" or op == "rectclip") {
        let saved = m.graphics
        let cleared = dispatch(m, "newpath", [])^
        let a = build_path(m, "moveto", [args[0], args[1]])^
        let b = build_path(m, "lineto", [args[0] + args[2], args[1]])^
        let c = build_path(m, "lineto", [args[0] + args[2], args[1] + args[3]])^
        let d = build_path(m, "lineto", [args[0], args[1] + args[3]])^
        let e = build_path(m, "closepath", [])^
        let painted = if (op == "rectclip") push_clip(m)^
            else paint(m, if (op == "rectfill") "fill" else "stroke")^
        m.graphics = {*: saved, clips: m.graphics.clips}
      ; return 0 }  raise obj.failure(m, "undefined graphics operator: " ++ op)
    return 0
}
