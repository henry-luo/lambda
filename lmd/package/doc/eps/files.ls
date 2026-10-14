import obj: .objects

pub let SIGNATURES = {'currentfile': "", 'readstring': "xx", 'readhexstring': "xx", 'readline': "xx",
    'read': "x", 'filter': "xx", 'token': "x", 'closefile': "x", 'status': "x", 'flushfile': "x"}

pub pn read_bytes(var m, value, kind, count) map^ {
    let e = obj.entry(m, value)^
    let valid = obj.require(m, e.kind == "file", "typecheck: expected file")^
    let stream = obj.values(m, value)^
    let readable = obj.require(m, stream.closed != true, "invalidaccess: closed file")^
    let remaining = m.limits.max_decoded_bytes - m.decoded_bytes
    let budget = obj.require(m, remaining > 0, "limitcheck: decoded bytes")^
    let result = ps_read(stream.source, stream.offset, {kind: kind, count: count, end: stream.end, limit: remaining})
    let checked = obj.require(m, result is map and result.error == null, if (result is map) result.error else "ioerror: byte reader")^
    m.heap[value.id].value.offset = result.next
    m.decoded_bytes = m.decoded_bytes + len(result.bytes)
    return result
}

pub pn dispatch(var m, op, args) int^ {
    if (op == "currentfile") { obj.push_operand(m, m.source); return 0 }
    if (op == "filter") {
        let e = obj.entry(m, args[0])^
        let supported = obj.require(m, e.kind in ["file", "string"], "typecheck: filter source")^
        let name = obj.key_name(m, args[1])^
        let known = obj.require(m, name in ["ASCII85Decode", "ASCIIHexDecode", "RunLengthDecode"], "undefined: unsupported filter " ++ name)^
        let source = if (e.kind == "file") args[0] else obj.allocate(m, "file", {source: obj.values(m, args[0])^, offset: 0, end: obj.object_length(m, args[0])^})^
        let decoded = read_bytes(m, source, name, 0)^
        let stream = obj.allocate(m, "file", {source: decoded.bytes, offset: 0, end: len(decoded.bytes)})^
        obj.push_operand(m, stream)
        return 0
    }
    if (op == "token") {
        let e = obj.entry(m, args[0])^
        let valid = obj.require(m, e.kind in ["string", "file"], "typecheck: token source")^
        let data = obj.values(m, args[0])^
        let readable = obj.require(m, e.kind != "file" or data.closed != true, "invalidaccess: closed file")^
        let bytes = if (e.kind == "file") data.source else data
        let offset = if (e.kind == "file") data.offset else 0
        let end = if (e.kind == "file") data.end else len(data)
        let token = ps_scan(bytes, offset, {*: m.limits, source_end: end})
        let checked = obj.require(m, token is map and token.kind != "error", "syntaxerror: token")^
        if (e.kind == "file") { m.heap[args[0].id].value.offset = token.next }
        if (token.kind == "eof") { obj.push_operand(m, false); return 0 }
        if (e.kind == "string") { obj.push_operand(m, {*: args[0], start: args[0].start + token.next, count: len(data) - token.next}) }
        obj.push_operand(m, obj.materialize(m, token)^); obj.push_operand(m, true)
        return 0
    }
    if (op == "read") {
        let result = read_bytes(m, args[0], "raw", 1)^
        if (len(result.bytes) > 0) { obj.push_operand(m, result.bytes[0]); obj.push_operand(m, true) }
        else { obj.push_operand(m, false) }
        return 0
    }
    if (op == "status" or op == "closefile" or op == "flushfile") {
        let e = obj.entry(m, args[0])^
        let checked = obj.require(m, e.kind == "file", "typecheck: file status")^
        let data = obj.values(m, args[0])^
        if (op == "status") { obj.push_operand(m, data.closed != true); return 0 }
        if (op == "flushfile") { m.heap[args[0].id].value.offset = data.end }
        if (op == "closefile") { m.heap[args[0].id].value.closed = true }
        return 0
    }
    let e = obj.entry(m, args[1])^
    let valid = obj.require(m, e.kind == "string", "typecheck: read destination")^
    let result = read_bytes(m, args[0], if (op == "readline") "line" else if (op == "readhexstring") "hex" else "raw", obj.object_length(m, args[1])^)^
    let stored = obj.put_sequence(m, args[1], 0, result.bytes)^
    obj.push_operand(m, {*: args[1], count: len(result.bytes)}); obj.push_operand(m, result.complete)
    return 0
}
