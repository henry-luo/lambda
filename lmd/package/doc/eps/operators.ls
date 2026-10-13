import obj: .objects
import gfx: .graphics
import matrices: .matrices
import vm: .vm

let CORE = {
    'pop': "x", 'dup': "x", 'exch': "xx", 'index': "i", 'roll': "ii", 'copy': "*", 'clear': "", 'count': "",
    'mark': "", 'cleartomark': "", 'counttomark': "", '[': "", ']': "", '<<': "", '>>': "",
    'array': "i", 'string': "i", 'dict': "i", 'begin': "d", 'end': "", 'def': "xx", 'load': "x", 'store': "xx",
    'get': "xx", 'put': "xxx", 'length': "x", 'known': "dx", 'where': "x", 'currentdict': "", 'systemdict': "", 'userdict': "",
    'aload': "a", 'astore': "a", 'getinterval': "xii", 'putinterval': "xix", 'cvx': "x", 'cvlit': "x", 'type': "x",
    'readonly': "x", 'noaccess': "x", 'executeonly': "x", 'rcheck': "x", 'wcheck': "x", 'xcheck': "x",
    'true': "", 'false': "", 'null': "", 'bind': "p", 'exec': "x", 'if': "bp", 'ifelse': "bpp",
    'repeat': "ip", 'for': "nnnp", 'loop': "p", 'exit': "", 'forall': "xp",
    'add': "nn", 'sub': "nn", 'mul': "nn", 'div': "nn", 'idiv': "ii", 'mod': "ii",
    'neg': "n", 'abs': "n", 'sqrt': "n", 'sin': "n", 'cos': "n", 'atan': "nn", 'exp': "nn", 'ln': "n", 'log': "n",
    'floor': "n", 'ceiling': "n", 'round': "n", 'truncate': "n", 'cvi': "n", 'cvr': "n",
    'eq': "xx", 'ne': "xx", 'lt': "nn", 'le': "nn", 'gt': "nn", 'ge': "nn", 'and': "bb", 'or': "bb", 'xor': "bb", 'not': "b",
    'showpage': ""
}
pub let SIGNATURES = {*: CORE, *: gfx.SIGNATURES, *: matrices.SIGNATURES, *: vm.SIGNATURES}
let ARITHMETIC = ["add", "sub", "mul", "div", "idiv", "mod", "neg", "abs", "sqrt", "sin", "cos", "atan", "exp", "ln", "log", "floor", "ceiling", "round", "truncate", "cvi", "cvr"]

fn matches(m, value, letter) bool^ {
    if (letter == "x") true
    else if (letter == "n") obj.is_number(value)
    else if (letter == "i") value is int
    else if (letter == "b") value is bool
    else if (not obj.is_ref(value)) false
    else {
        let e = obj.entry(m, value)^
        if (letter == "a") e.kind == "array"
        else if (letter == "d") e.kind == "dict"
        else e.kind == "array" and value.executable
    }
}

fn finite(v) => obj.is_number(v) and not (v is nan) and not (v is inf) and v != -inf

pub fn numeric(m, op, args) any^ {
    let a = args[0]
    let b = if (len(args) > 1) args[1] else 0
    let divisor = obj.require(m, not ((op == "div" or op == "idiv" or op == "mod") and b == 0), "undefinedresult: zero divisor")^
    let domain = obj.require(m, not ((op == "sqrt" and a < 0) or ((op == "ln" or op == "log") and a <= 0) or
        (op == "atan" and a == 0 and b == 0)), "undefinedresult: numeric domain")^
    let value = if (op == "add") float(a) + float(b) else if (op == "sub") float(a) - float(b) else if (op == "mul") float(a) * float(b)
        else if (op == "div") float(a) / float(b) else if (op == "idiv") int(float(a) / float(b))
        // guest remainder follows truncating division, independently of Lambda's % semantics.
        else if (op == "mod") a - int(float(a) / float(b)) * b
        else if (op == "neg") -a else if (op == "abs") abs(a) else if (op == "sqrt") math.sqrt(a)
        else if (op == "sin") math.sin(a * math.pi / 180.0) else if (op == "cos") math.cos(a * math.pi / 180.0)
        else if (op == "atan") {
            let angle = math.atan2(a, b) * 180.0 / math.pi
            if (angle < 0) angle + 360.0 else angle
        } else if (op == "exp") math.pow(a, b) else if (op == "ln") math.log(a) else if (op == "log") math.log10(a)
        else if (op == "floor") floor(a) else if (op == "ceiling") ceil(a)
        else if (op == "round") floor(a + 0.5) else if (op == "truncate" or op == "cvi") int(a)
        else float(a)
    let checked = obj.require(m, finite(value), "undefinedresult: numeric range")^
    if (op == "cvi" or op == "idiv") {
        let range_valid = obj.require(m, value >= -2147483648 and value <= 2147483647, "rangecheck: integer conversion")^
        int(value)
    } else if (op == "cvr" or op == "div" or op == "sqrt" or op == "sin" or op == "cos" or op == "atan" or
        op == "exp" or op == "ln" or op == "log" or a is float or b is float or value < -2147483648 or value > 2147483647) float(value)
    else int(value)
}

fn same(m, a, b) bool^ {
    if (obj.is_ref(a) and obj.is_ref(b)) {
        let left = obj.entry(m, a)^
        let right = obj.entry(m, b)^
        if (left.kind != right.kind) false
        else if (left.kind == "string") obj.values(m, a)^ == obj.values(m, b)^
        else a.id == b.id and a.start == b.start and obj.object_length(m, a)^ == obj.object_length(m, b)^
    } else if (a is map and b is map) a.kind == b.kind and a.value == b.value
    else a == b
}

pn mark_index(m) int^ {
    var index = len(m.stack) - 1
    while (index >= 0) {
        if (m.stack[index] is map and m.stack[index].kind == "mark") { return index }
        index = index - 1
    }
    raise obj.failure(m, "unmatchedmark")
}

pub pn dispatch(var m, op) int^ {
    let checked = obj.require(m, op at SIGNATURES, "undefined or unsupported: " ++ op)^
    let signature = SIGNATURES[op]
    let args = if (signature == "*") [] else obj.take_operands(m, len(signature))^
    if (signature != "*") {
        for (i in 0 to (len(signature) - 1)) {
            let valid = obj.require(m, matches(m, args[i], signature[i])^, "typecheck: " ++ op)^
        }
    }
    if (op at vm.SIGNATURES) { let applied = vm.dispatch(m, op, args)^; return 0 }
    if (op at gfx.SIGNATURES) { let applied = gfx.dispatch(m, op, args)^; return 0 }
    if (op at matrices.SIGNATURES) { let applied = matrices.dispatch(m, op, args)^; return 0 }
    if (op in ARITHMETIC) { push(m.stack, numeric(m, op, args)^); return 0 }
    if (op == "pop" or op == "showpage") { return 0 }
    if (op == "dup") { push(m.stack, args[0]); push(m.stack, args[0]); return 0 }
    if (op == "exch") { push(m.stack, args[1]); push(m.stack, args[0]); return 0 }
    if (op == "clear") { m.stack = []; return 0 }
    if (op == "count") { push(m.stack, len(m.stack)); return 0 }
    if (op == "index") {
        let n = args[0]
        let valid = obj.require(m, n >= 0 and n < len(m.stack), "rangecheck: index")^
        push(m.stack, m.stack[len(m.stack) - n - 1])
        return 0
    }
    if (op == "roll") {
        let n = args[0]
        let valid = obj.require(m, n >= 0 and n <= len(m.stack), "rangecheck: roll")^
        if (n > 0) {
            let tail = obj.take_operands(m, n)^
            let shift = args[1] - int(floor(float(args[1]) / n)) * n
            for (i in 0 to (n - 1)) { push(m.stack, tail[(i + n - shift) % n]) }
        }
        return 0
    }
    if (op == "mark" or op == "[" or op == "<<") { push(m.stack, {kind: "mark", value: op}); return 0 }
    if (op == "counttomark" or op == "cleartomark" or op == "]" or op == ">>") {
        let index = mark_index(m)^
        let tail = slice(m.stack, index + 1)
        if (op == "counttomark") { push(m.stack, len(tail)); return 0 }
        {
            m.stack = slice(m.stack, 0, index)
            if (op == "]") {
                let collection = obj.allocate(m, "array", tail)^
                push(m.stack, collection)
            } else if (op == ">>") {
                let even = obj.require(m, len(tail) % 2 == 0, "rangecheck: dictionary construction")^
                let dictionary = obj.allocate(m, "dict", {})^
                for (i in 0 to (len(tail) - 1) where i % 2 == 0) { let stored = obj.put_value(m, dictionary, tail[i], tail[i + 1])^ }
                push(m.stack, dictionary)
            }
        }
        return 0
    }
    if (op == "array" or op == "string" or op == "dict") {
        let n = args[0]
        let valid = obj.require(m, n >= 0 and n <= m.limits.max_composite_length, "rangecheck or limitcheck: allocation")^
        // S16.4.2v2: an empty pn control body yields null; build the map in value position.
        let empty_dictionary = {}
        let data = if (op == "dict") empty_dictionary else fill(n, if (op == "string") 0 else null)
        let value = obj.allocate(m, op, data)^
        push(m.stack, value)
        return 0
    }
    if (op == "begin") { let readable = obj.values(m, args[0])^; push(m.dicts, args[0]); return 0 }
    if (op == "end") {
        let valid = obj.require(m, len(m.dicts) > 2, "dictstackunderflow")^
        m.dicts = slice(m.dicts, 0, len(m.dicts) - 1)
        return 0
    }
    if (op == "def" or op == "store") {
        let key = obj.key_name(m, args[0])^
        let hit = obj.find_name(m, key)^
        let id = if (op == "store" and hit.found) hit.owner else m.dicts[len(m.dicts) - 1]
        let stored = obj.put_value(m, id, args[0], args[1])^
        return 0
    }
    if (op == "load") { push(m.stack, obj.lookup(m, obj.key_name(m, args[0])^)^); return 0 }
    if (op == "get") { push(m.stack, obj.get_value(m, args[0], args[1])^); return 0 }
    if (op == "put") { let stored = obj.put_value(m, args[0], args[1], args[2])^; return 0 }
    if (op == "length") { push(m.stack, obj.object_length(m, args[0])^); return 0 }
    if (op == "known") {
        let data = obj.values(m, args[0])^
        push(m.stack, obj.key_name(m, args[1])^ at data)
        return 0
    }
    if (op == "where") {
        let hit = obj.find_name(m, obj.key_name(m, args[0])^)^
        if (hit.found) { push(m.stack, hit.owner) }
        push(m.stack, hit.found)
        return 0
    }
    if (op == "currentdict" or op == "userdict" or op == "systemdict") {
        push(m.stack, if (op == "currentdict") m.dicts[len(m.dicts) - 1] else obj.reference(if (op == "systemdict") 0 else 1))
        return 0
    }
    if (op == "aload") {
        for (v in obj.values(m, args[0])^) { push(m.stack, v) }
        push(m.stack, args[0])
        return 0
    }
    if (op == "astore") {
        let data = obj.take_operands(m, obj.object_length(m, args[0])^)^
        for (i in 0 to (len(data) - 1)) { let stored = obj.put_value(m, args[0], i, data[i])^ }
        push(m.stack, args[0])
        return 0
    }
    if (op == "getinterval") {
        let n = obj.object_length(m, args[0])^
        let valid = obj.require(m, args[1] >= 0 and args[2] >= 0 and args[1] <= n and args[2] <= n - args[1], "rangecheck: interval")^
        let e = obj.entry(m, args[0])^
        let type_ok = obj.require(m, e.kind != "dict", "typecheck: interval")^
        push(m.stack, {*: args[0], start: args[0].start + args[1], count: args[2]})
        return 0
    }
    if (op == "putinterval") {
        let destination = obj.entry(m, args[0])^
        let source = obj.entry(m, args[2])^
        let compatible = obj.require(m, destination.kind in ["array", "string"] and source.kind == destination.kind, "typecheck: putinterval")^
        let data = obj.values(m, args[2])^
        let valid = obj.require(m, args[1] >= 0 and args[1] + len(data) <= obj.object_length(m, args[0])^, "rangecheck: putinterval")^
        for (i in 0 to (len(data) - 1)) { let stored = obj.put_value(m, args[0], args[1] + i, data[i])^ }
        return 0
    }
    if (op == "copy") {
        let top = obj.take_operands(m, 1)^
        if (top[0] is int) {
            let n = top[0]
            let valid = obj.require(m, n >= 0 and n <= len(m.stack), "rangecheck: copy")^
            let data = slice(m.stack, len(m.stack) - n)
            for (v in data) { push(m.stack, v) }
        } else {
            let source = obj.take_operands(m, 1)^
            let e = obj.entry(m, source[0])^
            let destination = obj.entry(m, top[0])^
            let compatible = obj.require(m, e.kind == destination.kind, "typecheck: copy")^
            let data = obj.values(m, source[0])^
            if (e.kind == "dict") {
                for (key, value in data) { let stored = obj.put_value(m, top[0], obj.literal_name(string(key)), value)^ }
            } else {
                for (i in 0 to (len(data) - 1)) { let stored = obj.put_value(m, top[0], i, data[i])^ }
            }
            push(m.stack, if (e.kind == "dict") top[0] else {*: top[0], count: len(data)})
        }
        return 0
    }
    if (op == "cvx" or op == "cvlit") {
        if (args[0] is map) { push(m.stack, {*: args[0], executable: op == "cvx"}) }
        else { push(m.stack, args[0]) }
        return 0
    }
    if (op == "readonly" or op == "noaccess" or op == "executeonly") {
        let e = obj.entry(m, args[0])^
        let permitted = obj.require(m, e.access != "noaccess" and not (e.access == "executeonly" and op == "readonly"), "invalidaccess")^
        push(m.stack, {*: args[0], access: op})
        return 0
    }
    if (op == "rcheck" or op == "wcheck" or op == "xcheck") {
        if (not obj.is_ref(args[0])) {
            push(m.stack, if (op == "xcheck") (args[0] is map and args[0].executable == true) else true)
            return 0
        }
        let e = obj.entry(m, args[0])^
        push(m.stack, if (op == "xcheck") args[0].executable else if (op == "wcheck") e.access == "unlimited" else e.access != "noaccess" and e.access != "executeonly")
        return 0
    }
    if (op == "type") {
        let v = args[0]
        let kind = if (v is int) "integer" else if (v is float) "real" else if (v is bool) "boolean"
            else if (v == null) "null" else if (obj.is_ref(v)) obj.entry(m, v)^.kind else v.kind
        push(m.stack, obj.literal_name(kind ++ "type"))
        return 0
    }
    if (op == "true" or op == "false" or op == "null") { push(m.stack, if (op == "null") null else op == "true"); return 0 }
    if (op == "bind") { let bound = obj.bind_object(m, args[0])^; push(m.stack, args[0]); return 0 }
    if (op == "exec") { let scheduled = obj.schedule(m, args[0])^; return 0 }
    if (op == "if" or op == "ifelse") {
        if (args[0] or op == "ifelse") { let scheduled = obj.schedule(m, args[if (args[0]) 1 else 2])^ }
        return 0
    }
    if (op == "repeat" or op == "loop") {
        let n = if (op == "loop") -1 else args[0]
        let valid = obj.require(m, op == "loop" or n >= 0, "rangecheck: repeat")^
        push(m.exec, {kind: "repeat", remaining: n, value: args[len(args) - 1]})
        return 0
    }
    if (op == "for") { push(m.exec, {kind: "for", current: args[0], step: args[1], limit: args[2], value: args[3]}); return 0 }
    if (op == "forall") {
        let e = obj.entry(m, args[0])^
        let data = obj.values(m, args[0])^
        push(m.exec, {kind: "forall", composite: args[0], value: args[1], index: 0, keys: if (e.kind == "dict") (data |> string(~key)) else null})
        return 0
    }
    if (op == "exit") {
        var index = len(m.exec) - 1
        while (index >= 0 and not (m.exec[index].kind in ["repeat", "for", "forall"])) { index = index - 1 }
        let valid = obj.require(m, index >= 0, "invalidexit")^
        m.exec = slice(m.exec, 0, index)
        return 0
    }
    if (op == "eq" or op == "ne") { push(m.stack, if (op == "eq") same(m, args[0], args[1])^ else not same(m, args[0], args[1])^); return 0 }
    if (op == "lt" or op == "le" or op == "gt" or op == "ge") {
        push(m.stack, if (op == "lt") args[0] < args[1] else if (op == "le") args[0] <= args[1] else if (op == "gt") args[0] > args[1] else args[0] >= args[1])
        return 0
    }
    if (op == "not") { push(m.stack, not args[0]); return 0 }
    if (op == "and" or op == "or" or op == "xor") {
        push(m.stack, if (op == "and") args[0] and args[1] else if (op == "or") args[0] or args[1] else args[0] != args[1])
      ; return 0 }  raise obj.failure(m, "unsupported: " ++ op)
    return 0
}
