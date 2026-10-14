// guest identities are explicit IDs; Lambda values retain S1.4 value semantics.
pub fn failure(m, reason) error => error("EPS " ++ reason ++ " at byte " ++ string(m.offset))
// S1.6: guest tags must survive host numeric array widening without changing PostScript types.
pub fn is_number(v) => v is int or v is float or (v is map and v.kind in ["integer", "real"])
pub fn is_integer(v) => v is int or (v is map and v.kind == "integer")
pub fn number_value(v) => if (v is map and v.kind == "integer") v.int_value else if (v is map and v.kind == "real") v.real_value else v
pub fn box(v) => if (v is int) {kind: "integer", int_value: v} else if (v is float) {kind: "real", real_value: v} else v
pub pn push_operand(var m, value) int { push(m.stack, box(value)); return 0 }
pub fn is_ref(v) => v is map and v.kind == "ref"
pub fn literal_name(value, executable = false) => {kind: "name", value: value, executable: executable}
pub fn reference(id, executable = false) => {kind: "ref", id: id, executable: executable, start: 0, count: -1, access: "unlimited"}

pub fn require(m, condition, reason) bool^ {
    if (condition) true else raise failure(m, reason)
}

pub fn entry(m, value) map^ {
    if (not is_ref(value) or value.id < 0 or value.id >= len(m.heap))
        raise failure(m, "typecheck: expected a composite composite")
    else {
        let e = m.heap[value.id]
        {*: e, access: if (e.access == "unlimited") value.access else e.access}
    }
}

pub fn values(m, value, executing = false) any^ {
    let e = entry(m, value)^
    if (e.access == "noaccess" or (e.access == "executeonly" and not executing)) raise failure(m, "invalidaccess")
    else if (value.count >= 0) slice(e.value, value.start, value.start + value.count)
    else e.value
}

pub fn object_length(m, value) int^ {
    let e = entry(m, value)^
    if (value.count >= 0) value.count else len(e.value)
}

pub pn allocate(var m, kind, value, executable = false) map^ {
    let checked = require(m, len(m.heap) < m.limits.max_objects and len(value) <= m.limits.max_composite_length, "limitcheck: composite allocation")^
    let id = len(m.heap)
    push(m.heap, {kind: kind, value: if (kind == "array") [for (v in value) box(v)] else value, access: "unlimited"})
    return reference(id, executable)
}

pub pn put_value(var m, composite, key, value) int^ {
    let e = entry(m, composite)^
    let writable = require(m, e.access == "unlimited", "invalidaccess")^
    if (e.kind == "dict") {
        let k = key_name(m, key)^
        m.heap[composite.id].value[k] = value
    } else {
        let index = number_value(key)
        let valid = require(m, is_integer(key) and index >= 0 and index < object_length(m, composite)^, "rangecheck: composite index")^
        let stored = if (e.kind == "string") number_value(value) else box(value)
        if (e.kind == "string") {
            let byte = require(m, is_integer(value) and stored >= 0 and stored <= 255, "rangecheck: string byte")^
        }
        m.heap[composite.id].value[composite.start + index] = stored
    }
    return 0
}

pub pn put_sequence(var m, composite, start, data) int^ {
    let e = entry(m, composite)^
    let valid = require(m, e.kind in ["array", "string"] and e.access == "unlimited" and
        start >= 0 and start + len(data) <= object_length(m, composite)^, "invalidaccess or rangecheck: sequence write")^
    if (e.kind == "string") {
        let bytes = require(m, all([for (v in data) is_integer(v) and number_value(v) >= 0 and number_value(v) <= 255]), "rangecheck: string byte")^
    }
    // keep one entry-time source snapshot for overlapping intervals and detach the destination once.
    for (i in 0 to (len(data) - 1)) { m.heap[composite.id].value[composite.start + start + i] = if (e.kind == "string") number_value(data[i]) else box(data[i]) }
    return 0
}

pub fn key_name(m, key) string^ {
    if (key is map and key.kind == "name") key.value
    else raise failure(m, "typecheck: dictionary key must be a name")
}

pub fn get_value(m, composite, key) any^ {
    let e = entry(m, composite)^
    let data = values(m, composite)^
    if (e.kind == "dict") {
        let k = key_name(m, key)^
        if (k at data) data[k] else raise failure(m, "undefined: " ++ k)
    } else if (not is_integer(key) or number_value(key) < 0 or number_value(key) >= len(data)) raise failure(m, "rangecheck: composite index")
    else data[number_value(key)]
}

pub pn find_name(m, key) map^ {
    var i = len(m.dicts) - 1
    while (i >= 0) {
        let d = values(m, m.dicts[i])^
        if (key at d) { return {found: true, value: d[key], owner: m.dicts[i]} }
        i = i - 1
    }
    return {found: false, value: null, owner: -1}
}

pub pn lookup(m, key) any^ {
    let hit = find_name(m, key)^
    if (not hit.found) raise failure(m, "undefined or unsupported: " ++ key)
    return hit.value
}

pub pn take_operands(var m, n) array^ {
    let checked = require(m, n >= 0 and len(m.stack) >= n, "stackunderflow")^
    let cut = len(m.stack) - n
    let args = slice(m.stack, cut)
    m.stack = slice(m.stack, 0, cut)
    return args
}

pub pn schedule(var m, value) int^ {
    if (is_ref(value) and value.executable) {
        let e = entry(m, value)^
        let valid = require(m, e.kind == "array", "unsupported executable composite")^
        push(m.exec, {kind: "procedure", value: value, index: 0})
    } else if (value is map and ((value.kind == "operator" and value.executable != false) or (value.kind == "name" and value.executable))) {
        push(m.exec, {kind: "composite", value: value})
    } else { push_operand(m, value) }
    return 0
}

pub pn materialize(var m, token) any^ {
    if (token.kind == "number") { return box(token.value) }
    if (token.kind == "string") { return allocate(m, "string", token.value)^ }
    if (token.kind == "immediate") { return lookup(m, token.value)^ }
    if (token.kind == "procedure") {
        var body = []
        for (child in token.value) { push(body, materialize(m, child)^) }
        return allocate(m, "array", body, true)^
    }
    return token
}

pub pn bind_object(var m, procedure, depth = 0) int^ {
    let e = entry(m, procedure)^
    let valid = require(m, e.kind == "array" and depth <= m.limits.max_procedure_depth, "typecheck or limitcheck: bind")^
    let body = values(m, procedure)^
    for (i in 0 to (len(body) - 1)) {
        let value = body[i]
        if (value is map and value.kind == "name" and value.executable) {
            let hit = find_name(m, value.value)^
            if (hit.found and hit.value is map and hit.value.kind == "operator") {
                let stored = put_value(m, procedure, i, {*: hit.value, offset: value.offset})^
            }
        } else if (is_ref(value) and value.executable) {
            let bound = bind_object(m, value, depth + 1)^
        }
    }
    return 0
}
