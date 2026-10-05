// jq-core in Lambda: the typed port of the C2MIR jq VM
// (test/benchmark/text/c2mir/jq_*.h) for the jq_* text benchmark rows; its
// time is reported in the C2MIR cell (vibe/impl/Lambda_Impl_Jq_Tests.md §5.5).
//
// Same design as the C VM: a recursive-descent parser, a compiler to
// bytecode resolving scopes statically, and a backtracking VM over jq's
// forkable data stack with fork records, try/label records, path tracking
// and heap frames. Values are native Lambda values (immutable, so updates
// copy along their path, as in C). Lambda containers have value semantics,
// so the shared mutable frames C keeps on its heap live here in arena
// arrays indexed by frame id, with a mark-compact pass standing in for the
// C GC's frame tracing (vm_compact_frames).

// ---------- opcodes (same numbering as jq_vm.h) ----------
let OP_DUP = 1
let OP_POP = 2
let OP_LOADK = 3
let OP_NEWARR = 4
let OP_OBJ_START = 5
let OP_SUBEXP_BEGIN = 6
let OP_SUBEXP_END = 7
let OP_INDEX = 8
let OP_INDEXK = 9
let OP_EACH = 10
let OP_SLICE = 11
let OP_FORK = 12
let OP_JUMP = 13
let OP_JUMP_F = 14
let OP_JUMP_F_SUB = 15
let OP_JUMP_F_KEEP = 16
let OP_BACKTRACK = 17
let OP_STOREV = 18
let OP_STOREV_UNDER = 19
let OP_LOADV = 20
let OP_LOADVN = 21
let OP_APPEND = 22
let OP_INSERT = 23
let OP_RANGE = 24
let OP_PATH_BEGIN = 25
let OP_PATH_END = 26
let OP_CALL_NATIVE = 27
let OP_CALL_JQ = 28
let OP_TAIL_CALL_JQ = 29
let OP_CALL_PARAM = 30
let OP_TAIL_CALL_PARAM = 31
let OP_RET = 32
let OP_TRY_BEGIN = 33
let OP_TRY_END = 34
let OP_LABEL_BEGIN = 35
let OP_BREAK = 36
let OP_BINOP = 37
let OP_NEG = 38
let OP_TOBOOL = 39

// binary operators
let OPB_ADD = 1
let OPB_SUB = 2
let OPB_MUL = 3
let OPB_DIV = 4
let OPB_MOD = 5
let OPB_EQ = 6
let OPB_NE = 7
let OPB_LT = 8
let OPB_LE = 9
let OPB_GT = 10
let OPB_GE = 11

// natives
let NAT_LENGTH = 1
let NAT_NOT = 2
let NAT_TYPE = 3
let NAT_KEYS = 4
let NAT_KEYS_UNSORTED = 5
let NAT_HAS = 6
let NAT_CONTAINS = 7
let NAT_TOSTRING = 8
let NAT_TOJSON = 9
let NAT_FROMJSON = 10
let NAT_TONUMBER = 11
let NAT_ASCII_UPCASE = 12
let NAT_ASCII_DOWNCASE = 13
let NAT_EXPLODE = 14
let NAT_IMPLODE = 15
let NAT_SPLIT = 16
let NAT_JOIN = 17
let NAT_ADD = 18
let NAT_FLATTEN = 19
let NAT_FLOOR = 20
let NAT_CEIL = 21
let NAT_MIN = 22
let NAT_MAX = 23
let NAT_UNIQUE = 24
let NAT_SORT = 25
let NAT_REVERSE = 26
let NAT_GETPATH = 27
let NAT_SETPATH = 28
let NAT_DELPATHS = 29
let NAT_FROM_ENTRIES = 30
let NAT_SORT_BY_IMPL = 31
let NAT_GROUP_BY_IMPL = 32
let NAT_ERROR0 = 33
let NAT_ERROR1 = 34

let native_names: string[] = ["length", "not", "type", "keys", "keys_unsorted", "has", "contains",
    "tostring", "tojson", "fromjson", "tonumber", "ascii_upcase", "ascii_downcase", "explode",
    "implode", "split", "join", "add", "flatten", "floor", "ceil", "min", "max", "unique", "sort",
    "reverse", "getpath", "setpath", "delpaths", "from_entries", "_sort_by_impl", "_group_by_impl",
    "error", "error"]
let native_arity: int[] = [0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 1, 0, 1, 1, 0, 1]

// ---------- values ----------

// jq type order: null < false < true < number < string < array < object
fn jtype(v) int {
    if (v == null) 0
    else if (v is bool) (if (v) 2 else 1)
    else if (v is int or v is float) 3
    else if (v is string) 4
    else if (v is array) 5
    else 6
}

fn type_name(v) string {
    let t = jtype(v)
    if (t == 0) "null"
    else if (t <= 2) "boolean"
    else if (t == 3) "number"
    else if (t == 4) "string"
    else if (t == 5) "array"
    else "object"
}

fn truthy(v) bool => not (v == null or v == false)

fn sorted_keys(o) string[] => sort([for (k at o) string(k)])

// jq's total order
pn jcmp(a, b) int {
    let ta = jtype(a)
    let tb = jtype(b)
    if (ta != tb) { return if (ta < tb) -1 else 1 }
    if (ta == 3) { return if (a < b) -1 else if (a > b) 1 else 0 }
    if (ta == 4) { return if (a < b) -1 else if (a > b) 1 else 0 }
    if (ta == 5) {
        var i: int = 0
        while (i < len(a) and i < len(b)) {
            let c = jcmp(a[i], b[i])
            if (c != 0) { return c }
            i = i + 1
        }
        return if (len(a) < len(b)) -1 else if (len(a) > len(b)) 1 else 0
    }
    if (ta == 6) {
        let ka: string[] = sorted_keys(a)
        let kb: string[] = sorted_keys(b)
        let ck = jcmp(ka, kb)
        if (ck != 0) { return ck }
        var j: int = 0
        while (j < len(ka)) {
            let c = jcmp(a[ka[j]], b[ka[j]])
            if (c != 0) { return c }
            j = j + 1
        }
        return 0
    }
    return 0
}

pn jequal(a, b) bool {
    jcmp(a, b) == 0
}

// stable merge sort of values by jcmp
pn jsort(xs: array) array {
    var items: array = xs
    let n: int = len(items)
    var width: int = 1
    while (width < n) {
        var out: array = []
        var lo: int = 0
        while (lo < n) {
            let mid: int = if (lo + width < n) lo + width else n
            let hi: int = if (lo + 2 * width < n) lo + 2 * width else n
            var i: int = lo
            var j: int = mid
            while (i < mid and j < hi) {
                if (jcmp(items[j], items[i]) < 0) {
                    out.push(items[j])
                    j = j + 1
                } else {
                    out.push(items[i])
                    i = i + 1
                }
            }
            while (i < mid) {
                out.push(items[i])
                i = i + 1
            }
            while (j < hi) {
                out.push(items[j])
                j = j + 1
            }
            lo = lo + 2 * width
        }
        items = out
        width = width * 2
    }
    items
}

// stable sort of indices by keys[index]
pn jsort_indices(keys: array) int[] {
    let n: int = len(keys)
    var idx: int[] = []
    var k: int = 0
    while (k < n) {
        idx.push(k)
        k = k + 1
    }
    var width: int = 1
    while (width < n) {
        var out: int[] = []
        var lo: int = 0
        while (lo < n) {
            let mid: int = if (lo + width < n) lo + width else n
            let hi: int = if (lo + 2 * width < n) lo + 2 * width else n
            var i: int = lo
            var j: int = mid
            while (i < mid and j < hi) {
                if (jcmp(keys[idx[j]], keys[idx[i]]) < 0) {
                    out.push(idx[j])
                    j = j + 1
                } else {
                    out.push(idx[i])
                    i = i + 1
                }
            }
            while (i < mid) {
                out.push(idx[i])
                i = i + 1
            }
            while (j < hi) {
                out.push(idx[j])
                j = j + 1
            }
            lo = lo + 2 * width
        }
        idx = out
        width = width * 2
    }
    idx
}

// a copy of object o with key set to v, keeping key order
pn obj_with(o, key: string, v) {
    var r = {}
    var found: bool = false
    for (k, x at o) {
        let name: string = string(k)
        if (name == key) {
            r[name] = v
            found = true
        } else {
            r[name] = x
        }
    }
    if (not found) { r[key] = v }
    r
}

// a copy of object o without key
pn obj_without(o, key: string) {
    var r = {}
    for (k, x at o) {
        let name: string = string(k)
        if (name != key) { r[name] = x }
    }
    r
}

// `{}` in an if-expression arm parses as an empty block; name the empty object
let jq_empty_object = {}

// a fresh copy of an array, safe to write (a `var` bound to a value reached
// through an expression can alias it, LR12-35)
fn copy_array(a) array => [for (x in a) x]

fn hex4(c: int) string {
    let digits: string = "0123456789abcdef"
    "\\u00" ++ slice(digits, c / 16, c / 16 + 1) ++ slice(digits, c % 16, c % 16 + 1)
}

pn json_string(s: string) string {
    var parts: string[] = ["\""]
    var i: int = 0
    while (i < len(s)) {
        let ch: string = slice(s, i, i + 1) or ""
        let c: int = ord(ch)
        parts.push(if (c == 34) "\\\""
            else if (c == 92) "\\\\"
            else if (c == 10) "\\n"
            else if (c == 9) "\\t"
            else if (c == 13) "\\r"
            else if (c == 8) "\\b"
            else if (c == 12) "\\f"
            else if (c < 32 or c == 127) hex4(c)
            else ch)
        i = i + 1
    }
    parts.push("\"")
    join(parts, "")
}

// jq's compact JSON text (Lambda's JSON writer only indents, LR09-32)
pn json_text(v) string {
    if (v == null) { return "null" }
    if (v is bool) { return if (v) "true" else "false" }
    if (v is int or v is float) { return string(v) }
    if (v is string) { return json_string(v) }
    if (v is array) { return "[" ++ join([for (x in v) json_text(x)], ",") ++ "]" }
    "{" ++ join([for (k, x at v) json_string(string(k)) ++ ":" ++ json_text(x)], ",") ++ "}"
}

pn tostring_value(v) string {
    if (v is string) { return v }
    json_text(v)
}

// "T (value)" as jq's error messages describe a value
pn describe(v) string {
    let text: string = json_text(v)
    type_name(v) ++ " (" ++ (if (len(text) > 11) slice(text, 0, 10) ++ "..." else text) ++ ")"
}

// ---------- error and value results ----------
// A native returns {ok: true, value: v} or {ok: false, value: error_value};
// the VM raises the latter as a jq error.
fn ok(v) => {ok: true, value: v}
fn fail(v) => {ok: false, value: v}

pn index_value(t, k) {
    let tt = jtype(t)
    let kt = jtype(k)
    if (tt == 0 and (kt == 4 or kt == 3 or kt == 0)) { return ok(null) }
    if (tt == 6 and kt == 4) { return ok(t[k]) }
    if (tt == 5 and kt == 3) {
        var d = floor(k)
        if (d < 0) { d = d + len(t) }
        if (d < 0 or d >= len(t)) { return ok(null) }
        return ok(t[int(d)])
    }
    let what: string = if (kt == 4) json_text(k) else type_name(k)
    fail("Cannot index " ++ type_name(t) ++ " with " ++ what)
}

pn slice_value(t, from, upto) {
    let tt = jtype(t)
    if (tt == 0) { return ok(null) }
    if ((from != null and jtype(from) != 3) or (upto != null and jtype(upto) != 3)) {
        return fail("Start and end indices of an array slice must be numbers")
    }
    if (tt != 4 and tt != 5) { return fail(describe(t) ++ " cannot be sliced") }
    let n: int = len(t)
    var s = if (from == null) 0 else from
    var e = if (upto == null) n else upto
    if (s < 0) { s = s + n }
    if (e < 0) { e = e + n }
    s = floor(s)
    e = ceil(e)
    if (s < 0) { s = 0 }
    if (e > n) { e = n }
    if (s > n) { s = n }
    if (e < s) { e = s }
    ok(slice(t, int(s), int(e)) or (if (tt == 4) "" else []))
}

pn binop_value(op: int, l, r) {
    let lt = jtype(l)
    let rt = jtype(r)
    if (op == OPB_ADD) {
        if (lt == 3 and rt == 3) { return ok(l + r) }
        if (lt == 0) { return ok(r) }
        if (rt == 0) { return ok(l) }
        if (lt == 4 and rt == 4) { return ok(l ++ r) }
        if (lt == 5 and rt == 5) { return ok(l ++ r) }
        if (lt == 6 and rt == 6) {
            var o = l
            for (k, x at r) { o = obj_with(o, string(k), x) }
            return ok(o)
        }
        return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be added")
    }
    if (op == OPB_SUB) {
        if (lt == 3 and rt == 3) { return ok(l - r) }
        if (lt == 5 and rt == 5) {
            var out: array = []
            var i: int = 0
            while (i < len(l)) {
                var keep: bool = true
                var j: int = 0
                while (j < len(r) and keep) {
                    if (jequal(l[i], r[j])) { keep = false }
                    j = j + 1
                }
                if (keep) { out.push(l[i]) }
                i = i + 1
            }
            return ok(out)
        }
        return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be subtracted")
    }
    if (op == OPB_MUL) {
        if (lt == 3 and rt == 3) { return ok(l * r) }
        if ((lt == 4 and rt == 3) or (lt == 3 and rt == 4)) {
            let s: string = if (lt == 4) l else r
            let n = if (lt == 3) l else r
            if (n <= 0) { return ok(null) }
            var parts: string[] = []
            var i: int = 0
            while (i < int(n) or i == 0) {
                parts.push(s)
                i = i + 1
            }
            return ok(join(parts, ""))
        }
        return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be multiplied")
    }
    if (op == OPB_DIV) {
        if (lt == 3 and rt == 3) {
            if (r == 0) { return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be divided because the divisor is zero") }
            return ok(l / r)
        }
        return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be divided")
    }
    if (op == OPB_MOD) {
        if (lt == 3 and rt == 3) {
            let a: int = int(l) ^ { 0 }
            var b: int = int(r) ^ { 0 }
            if (b == 0) { return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be divided because the divisor is zero") }
            if (b < 0) { b = 0 - b }
            return ok(a % b)
        }
        return fail(describe(l) ++ " and " ++ describe(r) ++ " cannot be divided")
    }
    let c = jcmp(l, r)
    if (op == OPB_EQ) { return ok(c == 0) }
    if (op == OPB_NE) { return ok(c != 0) }
    if (op == OPB_LT) { return ok(c < 0) }
    if (op == OPB_LE) { return ok(c <= 0) }
    if (op == OPB_GT) { return ok(c > 0) }
    ok(c >= 0)
}

// jq's jv_contains: a type mismatch below the top level is just false
pn contains_value(a, b) bool {
    let ta = jtype(a)
    let tb = jtype(b)
    if (ta == 6 and tb == 6) {
        for (k, y at b) {
            let name: string = string(k)
            if (a[name] == null and not (has_key(a, name))) { return false }
            if (not contains_value(a[name], y)) { return false }
        }
        return true
    }
    if (ta == 5 and tb == 5) {
        var j: int = 0
        while (j < len(b)) {
            var found: bool = false
            var i: int = 0
            while (i < len(a) and not found) {
                found = contains_value(a[i], b[j])
                i = i + 1
            }
            if (not found) { return false }
            j = j + 1
        }
        return true
    }
    if (ta == 4 and tb == 4) { return contains(a, b) }
    jequal(a, b)
}

fn has_key(o, name: string) bool => len([for (k at o where string(k) == name) k]) > 0

pn getpath_value(t, p: array) {
    var cur = t
    var i: int = 0
    while (i < len(p)) {
        if (cur == null) { return ok(null) }
        let r = index_value(cur, p[i])
        if (not r.ok) { return r }
        cur = r.value
        i = i + 1
    }
    ok(cur)
}

pn setpath_rec(t, p: array, i: int, v) {
    if (i == len(p)) { return ok(v) }
    let k = p[i]
    if (k is string) {
        let base = if (t == null) jq_empty_object else t
        if (jtype(base) != 6) { return index_value(base, k) }
        let child = setpath_rec(base[k], p, i + 1, v)
        if (not child.ok) { return child }
        return ok(obj_with(base, k, child.value))
    }
    if (k is int or k is float) {
        let base = if (t == null) [] else t
        if (jtype(base) != 5) {
            let bad = index_value(base, k)
            return if (bad.ok) fail("Cannot index " ++ type_name(base) ++ " with number") else bad
        }
        var d = floor(k)
        if (d < 0) { d = d + len(base) }
        if (d < 0) { return fail("Out of bounds negative array index") }
        let idx: int = int(d) ^ { 0 }
        let child = setpath_rec(if (idx < len(base)) base[idx] else null, p, i + 1, v)
        if (not child.ok) { return child }
        var out = copy_array(base)
        while (len(out) <= idx) { out.push(null) }
        out[idx] = child.value
        return ok(out)
    }
    fail("Invalid path component")
}

pn delpath_rec(t, p: array, i: int) {
    if (t == null) { return ok(null) }
    let k = p[i]
    let is_last: bool = i == len(p) - 1
    if (jtype(t) == 6 and k is string) {
        if (not has_key(t, k)) { return ok(t) }
        if (is_last) { return ok(obj_without(t, k)) }
        let child = delpath_rec(t[k], p, i + 1)
        if (not child.ok) { return child }
        return ok(obj_with(t, k, child.value))
    }
    if (jtype(t) == 5 and (k is int or k is float)) {
        var d = floor(k)
        if (d < 0) { d = d + len(t) }
        if (d < 0 or d >= len(t)) { return ok(t) }
        let idx: int = int(d) ^ { 0 }
        if (is_last) {
            var out: array = []
            var j: int = 0
            while (j < len(t)) {
                if (j != idx) { out.push(t[j]) }
                j = j + 1
            }
            return ok(out)
        }
        let child = delpath_rec(t[idx], p, i + 1)
        if (not child.ok) { return child }
        var out2 = copy_array(t)
        out2[idx] = child.value
        return ok(out2)
    }
    index_value(t, k)
}

pn delpaths_value(t, ps) {
    if (jtype(ps) != 5) { return fail("Paths must be specified as an array") }
    let sorted: array = jsort(ps)
    var cur = t
    var i: int = len(sorted) - 1
    while (i >= 0) {
        let p = sorted[i]
        if (jtype(p) != 5) { return fail("Path must be specified as an array") }
        if (len(p) == 0) { return ok(null) }
        let r = delpath_rec(cur, p, 0)
        if (not r.ok) { return r }
        cur = r.value
        i = i - 1
    }
    ok(cur)
}

pn flatten_into(a, var out: array) {
    var i: int = 0
    while (i < len(a)) {
        if (a[i] is array) { flatten_into(a[i], out) }
        else { out.push(a[i]) }
        i = i + 1
    }
}

pn codepoints(s: string) int[] {
    var out: int[] = []
    var i: int = 0
    while (i < len(s)) {
        out.push(ord(slice(s, i, i + 1)))
        i = i + 1
    }
    out
}

pn call_native(id: int, input, args: array) {
    let t = jtype(input)
    if (id == NAT_LENGTH) {
        if (t == 0) { return ok(0) }
        if (t == 3) { return ok(abs(input)) }
        if (t == 4 or t == 5 or t == 6) { return ok(len(input)) }
        return fail(describe(input) ++ " has no length")
    }
    if (id == NAT_NOT) { return ok(not truthy(input)) }
    if (id == NAT_TYPE) { return ok(type_name(input)) }
    if (id == NAT_KEYS or id == NAT_KEYS_UNSORTED) {
        if (t == 6) {
            return ok(if (id == NAT_KEYS) sorted_keys(input) else [for (k at input) string(k)])
        }
        if (t == 5) {
            var idx: int[] = []
            var i: int = 0
            while (i < len(input)) {
                idx.push(i)
                i = i + 1
            }
            return ok(idx)
        }
        return fail(describe(input) ++ " has no keys")
    }
    if (id == NAT_HAS) {
        if (t == 6 and args[0] is string) { return ok(has_key(input, args[0])) }
        if (t == 5 and jtype(args[0]) == 3) { return ok(args[0] >= 0 and args[0] < len(input)) }
        return fail(describe(input) ++ ", " ++ describe(args[0]) ++ ": cannot check whether one has a key")
    }
    if (id == NAT_CONTAINS) {
        let tb = jtype(args[0])
        let bools: bool = (t == 1 or t == 2) and (tb == 1 or tb == 2)
        if (t != tb and not bools) {
            return fail(describe(input) ++ " and " ++ describe(args[0]) ++ " cannot have their containment checked")
        }
        return ok(contains_value(input, args[0]))
    }
    if (id == NAT_TOSTRING) { return ok(tostring_value(input)) }
    if (id == NAT_TOJSON) { return ok(json_text(input)) }
    if (id == NAT_FROMJSON) {
        if (t != 4) { return fail(describe(input) ++ " cannot be parsed as JSON") }
        let parsed = parse(input, 'json') ^ { {jq_parse_failed: true} }
        if (parsed is map and parsed.jq_parse_failed == true) { return fail("Invalid JSON text: " ++ input) }
        return ok(parsed)
    }
    if (id == NAT_TONUMBER) {
        if (t == 3) { return ok(input) }
        if (t == 4) {
            let n = parse(input, 'json') ^ { null }
            if (n is int or n is float) { return ok(n) }
            return fail("Cannot parse '" ++ input ++ "' as JSON")
        }
        return fail(describe(input) ++ " cannot be parsed as a number")
    }
    if (id == NAT_ASCII_UPCASE or id == NAT_ASCII_DOWNCASE) {
        if (t != 4) { return fail(describe(input) ++ " cannot be case-converted") }
        let cps: int[] = codepoints(input)
        let mapped: string[] = [for (c in cps) chr(
            if (id == NAT_ASCII_UPCASE and c >= 97 and c <= 122) c - 32
            else if (id == NAT_ASCII_DOWNCASE and c >= 65 and c <= 90) c + 32
            else c)]
        return ok(join(mapped, ""))
    }
    if (id == NAT_EXPLODE) {
        if (t != 4) { return fail(describe(input) ++ " cannot be exploded") }
        return ok(codepoints(input))
    }
    if (id == NAT_IMPLODE) {
        if (t != 5) { return fail(describe(input) ++ " cannot be imploded") }
        return ok(join([for (c in input) chr(int(c))], ""))
    }
    if (id == NAT_SPLIT) {
        if (t != 4 or not (args[0] is string)) { return fail("split input and separator must be strings") }
        if (len(input) == 0) { return ok([]) }
        return ok(split(input, args[0]))
    }
    if (id == NAT_JOIN) {
        if (t != 5) { return fail(describe(input) ++ " cannot be joined") }
        let sep: string = if (args[0] is string) args[0] else ""
        return ok(join([for (x in input) if (x == null) "" else tostring_value(x)], sep))
    }
    if (id == NAT_ADD) {
        if (t == 0) { return ok(null) }
        if (t != 5) { return fail(describe(input) ++ " cannot be added up") }
        if (len(input) == 0) { return ok(null) }
        // accumulate into one fresh container (jq mutates its refcount-1 sum)
        var acc = input[0]
        var fresh_array: array = []
        var fresh_object = {}
        var mode: int = 0
        var i: int = 1
        while (i < len(input)) {
            let x = input[i]
            let at: int = if (mode == 1) 5 else if (mode == 2) 6 else jtype(acc)
            let xt = jtype(x)
            if (at == 5 and xt == 5) {
                if (mode != 1) {
                    fresh_array = copy_array(acc)
                    mode = 1
                }
                var j: int = 0
                while (j < len(x)) {
                    fresh_array.push(x[j])
                    j = j + 1
                }
            } else if (at == 6 and xt == 6) {
                if (mode != 2) {
                    fresh_object = {}
                    for (k0, y0 at acc) { fresh_object[string(k0)] = y0 }
                    mode = 2
                }
                for (k, y at x) { fresh_object[string(k)] = y }
            } else {
                if (mode == 1) { acc = fresh_array }
                if (mode == 2) { acc = fresh_object }
                mode = 0
                let r = binop_value(OPB_ADD, acc, x)
                if (not r.ok) { return r }
                acc = r.value
            }
            i = i + 1
        }
        return ok(if (mode == 1) fresh_array else if (mode == 2) fresh_object else acc)
    }
    if (id == NAT_FLATTEN) {
        if (t != 5) { return fail(describe(input) ++ " cannot be flattened") }
        var out: array = []
        flatten_into(input, out)
        return ok(out)
    }
    if (id == NAT_FLOOR or id == NAT_CEIL) {
        if (t != 3) { return fail(describe(input) ++ " number required") }
        return ok(if (id == NAT_FLOOR) floor(input) else ceil(input))
    }
    if (id == NAT_MIN or id == NAT_MAX) {
        if (t != 5) { return fail(describe(input) ++ " cannot be iterated") }
        if (len(input) == 0) { return ok(null) }
        var best = input[0]
        var i: int = 1
        while (i < len(input)) {
            let c = jcmp(input[i], best)
            if (if (id == NAT_MIN) c < 0 else c >= 0) { best = input[i] }
            i = i + 1
        }
        return ok(best)
    }
    if (id == NAT_SORT or id == NAT_UNIQUE) {
        if (t != 5) { return fail(describe(input) ++ " cannot be sorted, as it is not an array") }
        let sorted: array = jsort(input)
        if (id == NAT_SORT or len(sorted) < 2) { return ok(sorted) }
        var out: array = [sorted[0]]
        var i: int = 1
        while (i < len(sorted)) {
            if (not jequal(sorted[i], out[len(out) - 1])) { out.push(sorted[i]) }
            i = i + 1
        }
        return ok(out)
    }
    if (id == NAT_REVERSE) {
        if (t == 0) { return ok([]) }
        if (t == 4) { return ok(join(reverse([for (c in codepoints(input)) chr(c)]), "")) }
        if (t != 5) { return fail(describe(input) ++ " cannot be reversed") }
        return ok(reverse(input))
    }
    if (id == NAT_GETPATH) {
        if (jtype(args[0]) != 5) { return fail("Path must be specified as an array") }
        return getpath_value(input, args[0])
    }
    if (id == NAT_SETPATH) {
        if (jtype(args[0]) != 5) { return fail("Path must be specified as an array") }
        return setpath_rec(input, args[0], 0, args[1])
    }
    if (id == NAT_DELPATHS) { return delpaths_value(input, args[0]) }
    if (id == NAT_FROM_ENTRIES) {
        if (t != 5) { return fail(describe(input) ++ " cannot be iterated") }
        var r = {}
        let names: string[] = ["key", "k", "name", "Name", "K", "Key"]
        var i: int = 0
        while (i < len(input)) {
            let e = input[i]
            if (jtype(e) != 6) { return fail(describe(e) ++ " cannot be indexed as an entry") }
            var key = null
            var n: int = 0
            while (n < 6 and not truthy(key)) {
                key = e[names[n]]
                n = n + 1
            }
            let name: string = if (key is string) key else json_text(key)
            r[name] = if (has_key(e, "value")) e["value"] else e["v"]
            i = i + 1
        }
        return ok(r)
    }
    if (id == NAT_SORT_BY_IMPL or id == NAT_GROUP_BY_IMPL) {
        if (t != 5 or jtype(args[0]) != 5 or len(args[0]) != len(input)) {
            return fail(describe(input) ++ " cannot be sorted, as it is not an array")
        }
        let keys: array = args[0]
        let order: int[] = jsort_indices(keys)
        if (id == NAT_SORT_BY_IMPL) { return ok([for (i in order) input[i]]) }
        var groups: array = []
        var group: array = []
        var i: int = 0
        while (i < len(order)) {
            if (i > 0 and not jequal(keys[order[i]], keys[order[i - 1]])) {
                groups.push(group)
                group = []
            }
            group.push(input[order[i]])
            i = i + 1
        }
        if (len(order) > 0) { groups.push(group) }
        return ok(groups)
    }
    if (id == NAT_ERROR0) { return fail(input) }
    if (id == NAT_ERROR1) { return fail(args[0]) }
    fail("unknown native")
}

// ---------- parser (jq_parse.h) ----------

let N_IDENTITY = 1
let N_RECURSE_DEFAULT = 2
let N_LITERAL = 3
let N_INDEX = 4
let N_ITER = 5
let N_SLICE = 6
let N_TRY = 7
let N_ARRAY = 8
let N_OBJECT = 9
let N_NEG = 10
let N_BINOP = 11
let N_AND = 12
let N_OR = 13
let N_ALT = 14
let N_ASSIGN = 15
let N_UPDATE = 16
let N_ARITH_UPDATE = 17
let N_ALT_UPDATE = 18
let N_PIPE = 19
let N_COMMA = 20
let N_BIND = 21
let N_REDUCE = 22
let N_FOREACH = 23
let N_IF = 24
let N_FUNCDEF = 25
let N_CALL = 26
let N_VAR = 27
let N_LABEL = 28
let N_BREAK = 29

type Parser = {src: string, codes: int[], pos: int, n: int, failed: bool, msg: string}

// AST nodes are immutable maps: {kind, op, a, b, c, d, list, name, value, pvar}
fn mk(kind: int, a, b) => {kind: kind, op: 0, a: a, b: b, c: null, d: null, list: [],
    name: "", value: null, pvar: []}
fn mk_lit(v) => {kind: N_LITERAL, op: 0, a: null, b: null, c: null, d: null, list: [],
    name: "", value: v, pvar: []}
fn mk_named(kind: int, name: string, a, b) => {kind: kind, op: 0, a: a, b: b, c: null,
    d: null, list: [], name: name, value: null, pvar: []}
fn mk_binop(op: int, a, b) => {kind: N_BINOP, op: op, a: a, b: b, c: null, d: null,
    list: [], name: "", value: null, pvar: []}
fn mk_call(name: string, args: array) => {kind: N_CALL, op: 0, a: null, b: null, c: null,
    d: null, list: args, name: name, value: null, pvar: []}

pn parse_fail(var p: Parser, what: string) {
    if (not p.failed) {
        p.failed = true
        p.msg = "jq parse error: " ++ what ++ " at offset " ++ string(p.pos)
    }
}

fn is_ident_start(c: int) bool => (c >= 97 and c <= 122) or (c >= 65 and c <= 90) or c == 95
fn is_ident_char(c: int) bool => is_ident_start(c) or (c >= 48 and c <= 57)

pn ch(p: Parser) int {
    if (p.pos < p.n) p.codes[p.pos] else -1
}

pn skip_ws(var p: Parser) {
    while (p.pos < p.n) {
        let c: int = p.codes[p.pos]
        if (c == 32 or c == 9 or c == 10 or c == 13) {
            p.pos = p.pos + 1
        } else if (c == 35) {
            while (p.pos < p.n and p.codes[p.pos] != 10) { p.pos = p.pos + 1 }
        } else {
            break
        }
    }
}

pn peek(var p: Parser, tok: string) bool {
    skip_ws(p)
    let n: int = len(tok)
    p.pos + n <= p.n and slice(p.src, p.pos, p.pos + n) == tok
}

pn accept(var p: Parser, tok: string) bool {
    if (peek(p, tok)) {
        p.pos = p.pos + len(tok)
        return true
    }
    false
}

pn expect(var p: Parser, tok: string) {
    if (not accept(p, tok)) { parse_fail(p, "expected '" ++ tok ++ "'") }
}

pn read_ident(var p: Parser) string {
    skip_ws(p)
    let start: int = p.pos
    if (p.pos >= p.n or not is_ident_start(p.codes[p.pos])) { return "" }
    while (p.pos < p.n and is_ident_char(p.codes[p.pos])) { p.pos = p.pos + 1 }
    slice(p.src, start, p.pos) or ""
}

pn peek_keyword(var p: Parser, kw: string) bool {
    skip_ws(p)
    let n: int = len(kw)
    p.pos + n <= p.n and slice(p.src, p.pos, p.pos + n) == kw and
        (p.pos + n == p.n or not is_ident_char(p.codes[p.pos + n]))
}

pn accept_keyword(var p: Parser, kw: string) bool {
    if (not peek_keyword(p, kw)) { return false }
    p.pos = p.pos + len(kw)
    true
}

pn read_var(var p: Parser) string {
    if (not accept(p, "$")) {
        parse_fail(p, "expected $variable")
        return "?"
    }
    let name: string = read_ident(p)
    if (name == "") { parse_fail(p, "expected variable name") }
    name
}

fn hex_value(c: int) int {
    if (c >= 48 and c <= 57) c - 48
    else if (c >= 97 and c <= 102) c - 87
    else if (c >= 65 and c <= 70) c - 55
    else 0
}

fn codes_text(codes: int[]) string => join([for (c in codes) chr(c)], "")

// string literal with \(...) interpolation, built as "a" + (e|tostring) + ...
pn parse_string(var p: Parser) {
    var codes: int[] = []
    var result = null
    p.pos = p.pos + 1
    while (true) {
        if (p.pos >= p.n) {
            parse_fail(p, "unterminated string")
            break
        }
        let c: int = p.codes[p.pos]
        p.pos = p.pos + 1
        if (c == 34) { break }
        if (c != 92) {
            codes.push(c)
            continue
        }
        let e: int = ch(p)
        p.pos = p.pos + 1
        if (e == 40) {
            let piece = mk_lit(codes_text(codes))
            codes = []
            let expr = parse_pipe(p)
            expect(p, ")")
            let text = mk(N_PIPE, expr, mk_call("tostring", []))
            let joined = mk_binop(OPB_ADD, piece, text)
            result = if (result == null) joined else mk_binop(OPB_ADD, result, joined)
            continue
        }
        if (e == 117) {
            var cp: int = 0
            var k: int = 0
            while (k < 4 and p.pos < p.n) {
                cp = cp * 16 + hex_value(p.codes[p.pos])
                p.pos = p.pos + 1
                k = k + 1
            }
            codes.push(cp)
            continue
        }
        codes.push(if (e == 110) 10 else if (e == 116) 9 else if (e == 114) 13
            else if (e == 98) 8 else if (e == 102) 12 else e)
    }
    let tail = mk_lit(codes_text(codes))
    if (result == null) tail else mk_binop(OPB_ADD, result, tail)
}

pn parse_number(var p: Parser) {
    let start: int = p.pos
    while (p.pos < p.n) {
        let c: int = p.codes[p.pos]
        if ((c >= 48 and c <= 57) or c == 46) {
            p.pos = p.pos + 1
        } else if ((c == 101 or c == 69) and p.pos + 1 < p.n) {
            p.pos = p.pos + 1
            if (p.codes[p.pos] == 43 or p.codes[p.pos] == 45) { p.pos = p.pos + 1 }
        } else {
            break
        }
    }
    let text: string = slice(p.src, start, p.pos) or "0"
    mk_lit(parse(text, 'json') ^ { 0 })
}

// object value: ExpD := ExpD '|' ExpD | '-' ExpD | Term
pn parse_object_value(var p: Parser) {
    if (accept(p, "-")) { return mk(N_NEG, parse_object_value(p), null) }
    let v = parse_postfix(p, false)
    if (not peek(p, "|=") and accept(p, "|")) { return mk(N_PIPE, v, parse_object_value(p)) }
    v
}

pn parse_object(var p: Parser) {
    var pairs: array = []
    if (accept(p, "}")) { return {*: mk(N_OBJECT, null, null), list: pairs} }
    while (true) {
        skip_ws(p)
        var key = null
        var val = null
        if (peek(p, "$")) {
            let name: string = read_var(p)
            key = mk_lit(name)
            val = mk_named(N_VAR, name, null, null)
        } else {
            if (peek(p, "\"")) {
                key = parse_string(p)
            } else if (accept(p, "(")) {
                key = parse_pipe(p)
                expect(p, ")")
            } else {
                let id: string = read_ident(p)
                if (id == "") {
                    parse_fail(p, "bad object key")
                    return mk(N_OBJECT, null, null)
                }
                key = mk_lit(id)
            }
            if (accept(p, ":")) {
                val = parse_object_value(p)
            } else {
                // {a} is {a: .a}
                val = mk(N_INDEX, mk(N_IDENTITY, null, null), key)
            }
        }
        pairs.push(key)
        pairs.push(val)
        if (not accept(p, ",")) {
            expect(p, "}")
            break
        }
    }
    {*: mk(N_OBJECT, null, null), list: pairs}
}

// suffixes: .foo  ."str"  [e]  []  [e:e]  ?  and `as $x | body`
pn parse_suffixes(var p: Parser, start, allow_as: bool) {
    var term = start
    while (true) {
        skip_ws(p)
        let c: int = ch(p)
        let next: int = if (p.pos + 1 < p.n) p.codes[p.pos + 1] else -1
        if (c == 46 and (is_ident_start(next) or next == 34 or next == 91)) {
            p.pos = p.pos + 1
            if (next == 34) {
                term = mk(N_INDEX, term, parse_string(p))
            } else if (next != 91) {
                term = mk(N_INDEX, term, mk_lit(read_ident(p)))
            }
        } else if (accept(p, "[")) {
            if (accept(p, "]")) {
                term = mk(N_ITER, term, null)
            } else if (accept(p, ":")) {
                let upper = parse_pipe(p)
                expect(p, "]")
                term = {*: mk(N_SLICE, term, null), c: upper}
            } else {
                let idx = parse_pipe(p)
                if (accept(p, ":")) {
                    let upper = if (peek(p, "]")) null else parse_pipe(p)
                    expect(p, "]")
                    term = {*: mk(N_SLICE, term, idx), c: upper}
                } else {
                    expect(p, "]")
                    term = mk(N_INDEX, term, idx)
                }
            }
        } else if (not peek(p, "?//") and accept(p, "?")) {
            term = mk(N_TRY, term, null)
        } else {
            break
        }
    }
    if (allow_as and peek_keyword(p, "as")) {
        accept_keyword(p, "as")
        let name: string = read_var(p)
        expect(p, "|")
        return mk_named(N_BIND, name, term, parse_pipe(p))
    }
    term
}

pn parse_primary(var p: Parser) {
    skip_ws(p)
    if (p.pos >= p.n) {
        parse_fail(p, "unexpected end")
        return mk(N_IDENTITY, null, null)
    }
    let c: int = p.codes[p.pos]
    if (c == 46) {
        if (accept(p, "..")) { return mk(N_RECURSE_DEFAULT, null, null) }
        p.pos = p.pos + 1
        if (p.pos < p.n and is_ident_start(p.codes[p.pos])) {
            return mk(N_INDEX, mk(N_IDENTITY, null, null), mk_lit(read_ident(p)))
        }
        if (p.pos < p.n and p.codes[p.pos] == 34) {
            return mk(N_INDEX, mk(N_IDENTITY, null, null), parse_string(p))
        }
        return mk(N_IDENTITY, null, null)
    }
    if (c >= 48 and c <= 57) { return parse_number(p) }
    if (c == 34) { return parse_string(p) }
    if (accept(p, "(")) {
        let e = parse_pipe(p)
        expect(p, ")")
        return e
    }
    if (accept(p, "[")) {
        if (accept(p, "]")) { return mk(N_ARRAY, null, null) }
        let body = parse_pipe(p)
        expect(p, "]")
        return mk(N_ARRAY, body, null)
    }
    if (accept(p, "{")) { return parse_object(p) }
    if (c == 36) { return mk_named(N_VAR, read_var(p), null, null) }
    if (accept_keyword(p, "if")) { return parse_if(p) }
    if (accept_keyword(p, "try")) {
        let body = parse_postfix(p, false)
        let handler = if (accept_keyword(p, "catch")) parse_postfix(p, false) else null
        return mk(N_TRY, body, handler)
    }
    if (peek_keyword(p, "reduce") or peek_keyword(p, "foreach")) {
        let is_foreach: bool = accept_keyword(p, "foreach")
        if (not is_foreach) { accept_keyword(p, "reduce") }
        let source = parse_postfix(p, false)
        if (not accept_keyword(p, "as")) { parse_fail(p, "expected as") }
        let name: string = read_var(p)
        expect(p, "(")
        let init = parse_pipe(p)
        expect(p, ";")
        let update = parse_pipe(p)
        let extract = if (is_foreach and accept(p, ";")) parse_pipe(p) else null
        expect(p, ")")
        return {kind: if (is_foreach) N_FOREACH else N_REDUCE, op: 0, a: source, b: init,
            c: update, d: extract, list: [], name: name, value: null, pvar: []}
    }
    if (accept_keyword(p, "label")) {
        let name: string = read_var(p)
        expect(p, "|")
        return mk_named(N_LABEL, name, parse_pipe(p), null)
    }
    if (accept_keyword(p, "break")) { return mk_named(N_BREAK, read_var(p), null, null) }
    if (is_ident_start(c)) {
        let name: string = read_ident(p)
        var args: array = []
        if (accept(p, "(")) {
            while (true) {
                args.push(parse_pipe(p))
                if (not accept(p, ";")) {
                    expect(p, ")")
                    break
                }
            }
        }
        return mk_call(name, args)
    }
    parse_fail(p, "unexpected character")
    p.pos = p.pos + 1
    mk(N_IDENTITY, null, null)
}

// if c then t (elif c then t)* (else e)? end, with elif as nested ifs
pn parse_if(var p: Parser) {
    let cond = parse_pipe(p)
    if (not accept_keyword(p, "then")) { parse_fail(p, "expected then") }
    let then_branch = parse_pipe(p)
    var else_branch = null
    if (accept_keyword(p, "elif")) {
        else_branch = parse_if(p)
        return {*: mk(N_IF, cond, then_branch), c: else_branch}
    }
    if (accept_keyword(p, "else")) { else_branch = parse_pipe(p) }
    if (not accept_keyword(p, "end")) { parse_fail(p, "expected end") }
    {*: mk(N_IF, cond, then_branch), c: else_branch}
}

pn parse_postfix(var p: Parser, allow_as: bool) {
    parse_suffixes(p, parse_primary(p), allow_as)
}

pn parse_unary(var p: Parser) {
    skip_ws(p)
    if (ch(p) == 45 and not peek(p, "-=")) {
        p.pos = p.pos + 1
        return mk(N_NEG, parse_postfix(p, true), null)
    }
    parse_postfix(p, true)
}

pn parse_mul(var p: Parser) {
    var l = parse_unary(p)
    while (true) {
        if (peek(p, "*=") or peek(p, "/=") or peek(p, "%=")) { break }
        if (accept(p, "*")) { l = mk_binop(OPB_MUL, l, parse_unary(p)) }
        else if (not peek(p, "//") and accept(p, "/")) { l = mk_binop(OPB_DIV, l, parse_unary(p)) }
        else if (accept(p, "%")) { l = mk_binop(OPB_MOD, l, parse_unary(p)) }
        else { break }
    }
    l
}

pn parse_add(var p: Parser) {
    var l = parse_mul(p)
    while (true) {
        if (peek(p, "+=") or peek(p, "-=")) { break }
        if (accept(p, "+")) { l = mk_binop(OPB_ADD, l, parse_mul(p)) }
        else if (accept(p, "-")) { l = mk_binop(OPB_SUB, l, parse_mul(p)) }
        else { break }
    }
    l
}

pn parse_cmp(var p: Parser) {
    let l = parse_add(p)
    if (accept(p, "==")) { return mk_binop(OPB_EQ, l, parse_add(p)) }
    if (accept(p, "!=")) { return mk_binop(OPB_NE, l, parse_add(p)) }
    if (accept(p, "<=")) { return mk_binop(OPB_LE, l, parse_add(p)) }
    if (accept(p, ">=")) { return mk_binop(OPB_GE, l, parse_add(p)) }
    if (accept(p, "<")) { return mk_binop(OPB_LT, l, parse_add(p)) }
    if (accept(p, ">")) { return mk_binop(OPB_GT, l, parse_add(p)) }
    l
}

pn parse_and(var p: Parser) {
    var l = parse_cmp(p)
    while (accept_keyword(p, "and")) { l = mk(N_AND, l, parse_cmp(p)) }
    l
}

pn parse_or(var p: Parser) {
    var l = parse_and(p)
    while (accept_keyword(p, "or")) { l = mk(N_OR, l, parse_and(p)) }
    l
}

pn parse_assign(var p: Parser) {
    let l = parse_or(p)
    var kind: int = 0
    var op: int = 0
    if (accept(p, "|=")) { kind = N_UPDATE }
    else if (accept(p, "+=")) {
        kind = N_ARITH_UPDATE
        op = OPB_ADD
    } else if (accept(p, "-=")) {
        kind = N_ARITH_UPDATE
        op = OPB_SUB
    } else if (accept(p, "*=")) {
        kind = N_ARITH_UPDATE
        op = OPB_MUL
    } else if (accept(p, "/=")) {
        kind = N_ARITH_UPDATE
        op = OPB_DIV
    } else if (accept(p, "%=")) {
        kind = N_ARITH_UPDATE
        op = OPB_MOD
    } else if (accept(p, "//=")) {
        kind = N_ALT_UPDATE
    } else if (not peek(p, "==") and accept(p, "=")) {
        kind = N_ASSIGN
    }
    if (kind == 0) { return l }
    {*: mk(kind, l, parse_alt(p)), op: op}
}

pn parse_alt(var p: Parser) {
    let l = parse_assign(p)
    if (not peek(p, "//=") and accept(p, "//")) { return mk(N_ALT, l, parse_alt(p)) }
    l
}

pn parse_comma(var p: Parser) {
    var l = parse_alt(p)
    while (accept(p, ",")) { l = mk(N_COMMA, l, parse_alt(p)) }
    l
}

pn parse_funcdef(var p: Parser) {
    let name: string = read_ident(p)
    if (name == "") { parse_fail(p, "expected function name") }
    var params: array = []
    var pvar: bool[] = []
    if (accept(p, "(")) {
        while (true) {
            let is_var: bool = peek(p, "$")
            let pname: string = if (is_var) read_var(p) else read_ident(p)
            if (pname == "") {
                parse_fail(p, "expected parameter")
                break
            }
            params.push(mk_named(N_VAR, pname, null, null))
            pvar.push(is_var)
            if (not accept(p, ";")) {
                expect(p, ")")
                break
            }
        }
    }
    expect(p, ":")
    let body = parse_pipe(p)
    expect(p, ";")
    let rest = parse_pipe(p)
    {kind: N_FUNCDEF, op: 0, a: body, b: rest, c: null, d: null, list: params, name: name,
        value: null, pvar: pvar}
}

pn parse_pipe(var p: Parser) {
    if (accept_keyword(p, "def")) { return parse_funcdef(p) }
    let l = parse_comma(p)
    if (not peek(p, "|=") and accept(p, "|")) { return mk(N_PIPE, l, parse_pipe(p)) }
    l
}

// ---------- compiler (jq_vm.h) ----------

let SC_VAR = 1
let SC_DEF = 2
let SC_PARAM = 3

// jq 1.7.1 src/builtin.jq definitions used by jq-core (the same text as C)
let jq_prelude: string = "def select(f): if f then . else empty end;\n" ++
    "def recurse(f): def r: ., (f | r); r;\n" ++
    "def recurse: recurse(.[]?);\n" ++
    "def map(f): [.[] | f];\n" ++
    "def to_entries: [keys_unsorted[] as $k | {key: $k, value: .[$k]}];\n" ++
    "def with_entries(f): to_entries | map(f) | from_entries;\n" ++
    "def paths: path(..) | select(length > 0);\n" ++
    "def paths(node_filter): . as $dot | paths | select(. as $p | $dot | getpath($p) | node_filter);\n" ++
    "def del(f): delpaths([path(f)]);\n" ++
    "def _assign(paths; $value): reduce path(paths) as $p (.; setpath($p; $value));\n" ++
    "def _modify(paths; update): reduce path(paths) as $p (.; . as $x | label $out" ++
    " | (setpath($p; $x | getpath($p) | update) | ., break $out), delpaths([$p]));\n" ++
    "def first(f): label $out | f | ., break $out;\n" ++
    "def last(f): reduce f as $x (null; $x);\n" ++
    "def limit($n; f): if $n > 0 then label $out | foreach f as $item (0; . + 1; $item," ++
    " if . >= $n then break $out else empty end) elif $n == 0 then empty else f end;\n" ++
    "def nth($n; f): if $n < 0 then error(\"Out of bounds negative array index\")" ++
    " else last(limit($n + 1; f)) end;\n" ++
    "def repeat(f): def _repeat: f, _repeat; _repeat;\n" ++
    "def until(cond; update): def _until: if cond then . else (update | _until) end; _until;\n" ++
    "def first: .[0];\n" ++
    "def last: .[-1];\n" ++
    "def sort_by(f): _sort_by_impl(map([f]));\n" ++
    "def group_by(f): _group_by_impl(map([f]));\n" ++
    "def unique_by(f): [group_by(f)[] | .[0]];\n" ++
    "def scalars: select(type | . != \"array\" and . != \"object\");\n" ++
    "def objects: select(type == \"object\");\n" ++
    "def arrays: select(type == \"array\");\n" ++
    "def numbers: select(type == \"number\");\n" ++
    "def strings: select(type == \"string\");\n" ++
    "def range($x): range(0; $x);\n" ++
    "def isempty(g): first((g | false), true);\n" ++
    "def any: reduce .[] as $x (false; . or $x);\n" ++
    "def all: reduce .[] as $x (true; . and $x);\n"

type Comp = {code: int[], consts: array, fn_entry: int[], fn_nparams: int[], fn_nlocals: int[],
    ctx_parent: int[], ctx_level: int[], ctx_fn: int[], ctx_scope: int[], ctx_nlocals: int[],
    sc_kind: int[], sc_name: string[], sc_arity: int[], sc_index: int[], sc_prev: int[],
    failed: bool, msg: string, synth: int}

pn compile_fail(var cs: Comp, what: string, name: string) {
    if (not cs.failed) {
        cs.failed = true
        cs.msg = "jq compile error: " ++ what ++ " " ++ name
    }
}

pn emit_op(var cs: Comp, x: int) int {
    cs.code.push(x)
    len(cs.code) - 1
}

pn add_const(var cs: Comp, v) int {
    cs.consts.push(v)
    len(cs.consts) - 1
}

pn patch_here(var cs: Comp, at: int) {
    cs.code[at] = len(cs.code)
}

pn new_fn(var cs: Comp, nparams: int) int {
    cs.fn_entry.push(0)
    cs.fn_nparams.push(nparams)
    cs.fn_nlocals.push(0)
    len(cs.fn_entry) - 1
}

pn new_ctx(var cs: Comp, parent: int, fn_index: int) int {
    cs.ctx_parent.push(parent)
    cs.ctx_level.push(if (parent < 0) 0 else cs.ctx_level[parent] + 1)
    cs.ctx_fn.push(fn_index)
    cs.ctx_scope.push(-1)
    cs.ctx_nlocals.push(0)
    len(cs.ctx_parent) - 1
}

pn scope_push(var cs: Comp, c: int, kind: int, name: string, arity: int, index: int) {
    cs.sc_kind.push(kind)
    cs.sc_name.push(name)
    cs.sc_arity.push(arity)
    cs.sc_index.push(index)
    cs.sc_prev.push(cs.ctx_scope[c])
    cs.ctx_scope[c] = len(cs.sc_kind) - 1
}

pn new_local(var cs: Comp, c: int) int {
    let slot: int = cs.ctx_nlocals[c]
    cs.ctx_nlocals[c] = slot + 1
    slot
}

pn emit_var_op(var cs: Comp, op: int, hops: int, slot: int) {
    emit_op(cs, op)
    emit_op(cs, hops)
    emit_op(cs, slot)
}

// `code` with SUBEXP brackets: leaves [result, input] on the stack
pn compile_subexp(var cs: Comp, c: int, n) {
    emit_op(cs, OP_SUBEXP_BEGIN)
    compile(cs, c, n, false)
    emit_op(cs, OP_SUBEXP_END)
}

// [slot, hops] of a variable, resolved through the lexical contexts
pn lookup_var(var cs: Comp, c: int, name: string) int[] {
    var x: int = c
    while (x >= 0) {
        var s: int = cs.ctx_scope[x]
        while (s >= 0) {
            if (cs.sc_kind[s] == SC_VAR and cs.sc_name[s] == name) {
                return [cs.sc_index[s], cs.ctx_level[c] - cs.ctx_level[x]]
            }
            s = cs.sc_prev[s]
        }
        x = cs.ctx_parent[x]
    }
    compile_fail(cs, "undefined variable $", name);
    [0, 0]
}

// compile an argument as a closure: an inline function of the call site
pn compile_closure(var cs: Comp, c: int, body) int {
    emit_op(cs, OP_JUMP)
    let target: int = emit_op(cs, 0)
    let fn_index: int = new_fn(cs, 0)
    let inner: int = new_ctx(cs, c, fn_index)
    cs.fn_entry[fn_index] = len(cs.code)
    compile(cs, inner, body, true)
    emit_op(cs, OP_RET)
    cs.fn_nlocals[fn_index] = cs.ctx_nlocals[inner]
    patch_here(cs, target)
    fn_index
}

fn find_native(name: string, arity: int) int {
    let hits: int[] = [for (i in 0 to len(native_names) - 1 where native_names[i] == name and native_arity[i] == arity) i + 1]
    if (len(hits) > 0) hits[0] else 0
}

pn compile_call(var cs: Comp, c: int, n, tail: bool) {
    let name: string = n.name
    let args: array = n.list
    let arity: int = len(args)
    // user/prelude definitions and parameters, innermost first
    var x: int = c
    while (x >= 0) {
        var s: int = cs.ctx_scope[x]
        while (s >= 0) {
            if (cs.sc_name[s] == name) {
                if (cs.sc_kind[s] == SC_PARAM and arity == 0) {
                    emit_op(cs, if (tail) OP_TAIL_CALL_PARAM else OP_CALL_PARAM)
                    emit_op(cs, cs.ctx_level[c] - cs.ctx_level[x])
                    emit_op(cs, cs.sc_index[s])
                    return null
                }
                if (cs.sc_kind[s] == SC_DEF and cs.sc_arity[s] == arity) {
                    var closures: int[] = []
                    var i: int = 0
                    while (i < arity) {
                        closures.push(compile_closure(cs, c, args[i]))
                        i = i + 1
                    }
                    emit_op(cs, if (tail) OP_TAIL_CALL_JQ else OP_CALL_JQ)
                    emit_op(cs, cs.sc_index[s])
                    emit_op(cs, cs.ctx_level[c] - cs.ctx_level[x])
                    emit_op(cs, arity)
                    var j: int = 0
                    while (j < arity) {
                        emit_op(cs, closures[j])
                        j = j + 1
                    }
                    return null
                }
            }
            s = cs.sc_prev[s]
        }
        x = cs.ctx_parent[x]
    }
    if (arity == 0 and name == "empty") {
        emit_op(cs, OP_BACKTRACK)
        return null
    }
    if (arity == 0 and (name == "true" or name == "false" or name == "null")) {
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, if (name == "true") true else if (name == "false") false else null))
        return null
    }
    if (arity == 1 and name == "path") {
        emit_op(cs, OP_PATH_BEGIN)
        compile(cs, c, args[0], false)
        emit_op(cs, OP_PATH_END)
        return null
    }
    if ((arity == 2 or arity == 3) and name == "range") {
        var i: int = 0
        while (i < arity) {
            compile_subexp(cs, c, args[i])
            i = i + 1
        }
        emit_op(cs, OP_RANGE)
        emit_op(cs, arity)
        return null
    }
    let id: int = find_native(name, arity)
    if (id == 0) {
        compile_fail(cs, "undefined function", name)
        return null
    }
    var k: int = 0
    while (k < arity) {
        compile_subexp(cs, c, args[k])
        k = k + 1
    }
    emit_op(cs, OP_CALL_NATIVE)
    emit_op(cs, id)
    emit_op(cs, arity)
}

pn compile_funcdef(var cs: Comp, c: int, n, tail: bool) {
    let params: array = n.list
    let nparams: int = len(params)
    let fn_index: int = new_fn(cs, nparams)
    // visible in its own body (recursion) and in the rest of the scope
    scope_push(cs, c, SC_DEF, n.name, nparams, fn_index)
    emit_op(cs, OP_JUMP)
    let target: int = emit_op(cs, 0)
    let inner: int = new_ctx(cs, c, fn_index)
    cs.fn_entry[fn_index] = len(cs.code)
    var i: int = 0
    while (i < nparams) {
        scope_push(cs, inner, SC_PARAM, params[i].name, 0, i)
        i = i + 1
    }
    // def f($a): body  ==  def f(a): a as $a | body
    var body = n.a
    var j: int = nparams - 1
    while (j >= 0) {
        if (n.pvar[j]) {
            body = mk_named(N_BIND, params[j].name, mk_call(params[j].name, []), body)
        }
        j = j - 1
    }
    compile(cs, inner, body, true)
    emit_op(cs, OP_RET)
    cs.fn_nlocals[fn_index] = cs.ctx_nlocals[inner]
    patch_here(cs, target)
    compile(cs, c, n.b, tail)
}

pn synthetic_var(var cs: Comp) string {
    cs.synth = cs.synth + 1
    "__rhs" ++ string(cs.synth)
}

pn compile(var cs: Comp, c: int, n, tail: bool) {
    if (cs.failed) { return null }
    let kind: int = n.kind
    if (kind == N_IDENTITY) { return null }
    if (kind == N_RECURSE_DEFAULT) {
        compile_call(cs, c, mk_call("recurse", []), tail)
        return null
    }
    if (kind == N_LITERAL) {
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, n.value))
        return null
    }
    if (kind == N_VAR) {
        let at: int[] = lookup_var(cs, c, n.name)
        emit_var_op(cs, OP_LOADV, at[1], at[0])
        return null
    }
    if (kind == N_INDEX) {
        if (n.b.kind == N_LITERAL) {
            compile(cs, c, n.a, false)
            emit_op(cs, OP_INDEXK)
            emit_op(cs, add_const(cs, n.b.value))
            return null
        }
        // the key is evaluated against the term's own input
        compile_subexp(cs, c, n.b)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_INDEX)
        return null
    }
    if (kind == N_ITER) {
        compile(cs, c, n.a, false)
        emit_op(cs, OP_EACH)
        return null
    }
    if (kind == N_SLICE) {
        compile_subexp(cs, c, if (n.b == null) mk_lit(null) else n.b)
        compile_subexp(cs, c, if (n.c == null) mk_lit(null) else n.c)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_SLICE)
        return null
    }
    if (kind == N_TRY) {
        emit_op(cs, OP_TRY_BEGIN)
        let handler: int = emit_op(cs, 0)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_TRY_END)
        emit_op(cs, OP_JUMP)
        let end_at: int = emit_op(cs, 0)
        patch_here(cs, handler)
        if (n.b != null) { compile(cs, c, n.b, tail) }
        else { emit_op(cs, OP_BACKTRACK) }
        patch_here(cs, end_at)
        return null
    }
    if (kind == N_ARRAY) {
        if (n.a == null) {
            emit_op(cs, OP_NEWARR)
            return null
        }
        let slot: int = new_local(cs, c)
        emit_op(cs, OP_DUP)
        emit_op(cs, OP_NEWARR)
        emit_var_op(cs, OP_STOREV, 0, slot)
        emit_op(cs, OP_FORK)
        let done: int = emit_op(cs, 0)
        compile(cs, c, n.a, false)
        emit_var_op(cs, OP_APPEND, 0, slot)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, done)
        emit_var_op(cs, OP_LOADVN, 0, slot)
        return null
    }
    if (kind == N_OBJECT) {
        emit_op(cs, OP_OBJ_START)
        var i: int = 0
        while (i < len(n.list)) {
            compile_subexp(cs, c, n.list[i])
            compile_subexp(cs, c, n.list[i + 1])
            emit_op(cs, OP_INSERT)
            i = i + 2
        }
        emit_op(cs, OP_POP)
        return null
    }
    if (kind == N_NEG) {
        compile(cs, c, n.a, false)
        emit_op(cs, OP_NEG)
        return null
    }
    if (kind == N_BINOP) {
        // jq evaluates the right operand in the outer loop
        compile_subexp(cs, c, n.b)
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_BINOP)
        emit_op(cs, n.op)
        return null
    }
    if (kind == N_AND) {
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_JUMP_F_SUB)
        let no: int = emit_op(cs, 0)
        compile(cs, c, n.b, false)
        emit_op(cs, OP_TOBOOL)
        emit_op(cs, OP_JUMP)
        let end_at: int = emit_op(cs, 0)
        patch_here(cs, no)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, false))
        patch_here(cs, end_at)
        return null
    }
    if (kind == N_OR) {
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_JUMP_F_SUB)
        let other: int = emit_op(cs, 0)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, true))
        emit_op(cs, OP_JUMP)
        let end_at: int = emit_op(cs, 0)
        patch_here(cs, other)
        compile(cs, c, n.b, false)
        emit_op(cs, OP_TOBOOL)
        patch_here(cs, end_at)
        return null
    }
    if (kind == N_ALT) {
        // a // b: every truthy output of a (errors suppressed); b when none
        let found: int = new_local(cs, c)
        emit_op(cs, OP_DUP)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, false))
        emit_var_op(cs, OP_STOREV, 0, found)
        emit_op(cs, OP_FORK)
        let other: int = emit_op(cs, 0)
        emit_op(cs, OP_TRY_BEGIN)
        let skip: int = emit_op(cs, 0)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_TRY_END)
        emit_op(cs, OP_JUMP_F_KEEP)
        let skip2: int = emit_op(cs, 0)
        emit_op(cs, OP_DUP)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, true))
        emit_var_op(cs, OP_STOREV, 0, found)
        emit_op(cs, OP_JUMP)
        let end_at: int = emit_op(cs, 0)
        patch_here(cs, skip)
        patch_here(cs, skip2)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, other)
        emit_op(cs, OP_DUP)
        emit_var_op(cs, OP_LOADV, 0, found)
        emit_op(cs, OP_JUMP_F)
        let run: int = emit_op(cs, 0)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, run)
        compile(cs, c, n.b, tail)
        patch_here(cs, end_at)
        return null
    }
    if (kind == N_UPDATE) {
        compile_call(cs, c, mk_call("_modify", [n.a, n.b]), tail)
        return null
    }
    if (kind == N_ASSIGN) {
        compile_call(cs, c, mk_call("_assign", [n.a, n.b]), tail)
        return null
    }
    if (kind == N_ARITH_UPDATE or kind == N_ALT_UPDATE) {
        // a op= b  ==  b as $v | _modify(a; . op $v), b evaluated against .
        let name: string = synthetic_var(cs)
        let var_node = mk_named(N_VAR, name, null, null)
        let update = if (kind == N_ARITH_UPDATE) mk_binop(n.op, mk(N_IDENTITY, null, null), var_node)
            else mk(N_ALT, mk(N_IDENTITY, null, null), var_node)
        compile(cs, c, mk_named(N_BIND, name, n.b, mk_call("_modify", [n.a, update])), tail)
        return null
    }
    if (kind == N_PIPE) {
        compile(cs, c, n.a, false)
        compile(cs, c, n.b, tail)
        return null
    }
    if (kind == N_COMMA) {
        emit_op(cs, OP_FORK)
        let second: int = emit_op(cs, 0)
        compile(cs, c, n.a, tail)
        emit_op(cs, OP_JUMP)
        let end_at: int = emit_op(cs, 0)
        patch_here(cs, second)
        compile(cs, c, n.b, tail)
        patch_here(cs, end_at)
        return null
    }
    if (kind == N_BIND) {
        let slot: int = new_local(cs, c)
        compile_subexp(cs, c, n.a)
        emit_var_op(cs, OP_STOREV_UNDER, 0, slot)
        let saved: int = cs.ctx_scope[c]
        scope_push(cs, c, SC_VAR, n.name, 0, slot)
        compile(cs, c, n.b, tail)
        cs.ctx_scope[c] = saved
        return null
    }
    if (kind == N_REDUCE) {
        let acc: int = new_local(cs, c)
        let x: int = new_local(cs, c)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.b, false)
        emit_var_op(cs, OP_STOREV, 0, acc)
        emit_op(cs, OP_FORK)
        let end_at: int = emit_op(cs, 0)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.a, false)
        emit_var_op(cs, OP_STOREV, 0, x)
        emit_var_op(cs, OP_LOADVN, 0, acc)
        let saved: int = cs.ctx_scope[c]
        scope_push(cs, c, SC_VAR, n.name, 0, x)
        compile(cs, c, n.c, false)
        cs.ctx_scope[c] = saved
        emit_var_op(cs, OP_STOREV, 0, acc)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, end_at)
        emit_var_op(cs, OP_LOADVN, 0, acc)
        return null
    }
    if (kind == N_FOREACH) {
        let acc: int = new_local(cs, c)
        let x: int = new_local(cs, c)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.b, false)
        emit_var_op(cs, OP_STOREV, 0, acc)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.a, false)
        emit_var_op(cs, OP_STOREV, 0, x)
        emit_var_op(cs, OP_LOADV, 0, acc)
        let saved: int = cs.ctx_scope[c]
        scope_push(cs, c, SC_VAR, n.name, 0, x)
        compile(cs, c, n.c, false)
        emit_op(cs, OP_DUP)
        emit_var_op(cs, OP_STOREV, 0, acc)
        if (n.d != null) { compile(cs, c, n.d, tail) }
        cs.ctx_scope[c] = saved
        return null
    }
    if (kind == N_IF) {
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_JUMP_F_SUB)
        let other: int = emit_op(cs, 0)
        compile(cs, c, n.b, tail)
        emit_op(cs, OP_JUMP)
        let end_at: int = emit_op(cs, 0)
        patch_here(cs, other)
        if (n.c != null) { compile(cs, c, n.c, tail) }
        patch_here(cs, end_at)
        return null
    }
    if (kind == N_FUNCDEF) {
        let saved: int = cs.ctx_scope[c]
        compile_funcdef(cs, c, n, tail)
        cs.ctx_scope[c] = saved
        return null
    }
    if (kind == N_CALL) {
        compile_call(cs, c, n, tail)
        return null
    }
    if (kind == N_LABEL) {
        let slot: int = new_local(cs, c)
        emit_var_op(cs, OP_LABEL_BEGIN, 0, slot)
        let saved: int = cs.ctx_scope[c]
        scope_push(cs, c, SC_VAR, n.name, 0, slot)
        compile(cs, c, n.a, false)
        cs.ctx_scope[c] = saved
        emit_op(cs, OP_TRY_END)
        return null
    }
    if (kind == N_BREAK) {
        let at: int[] = lookup_var(cs, c, n.name)
        emit_var_op(cs, OP_BREAK, at[1], at[0])
        return null
    }
    compile_fail(cs, "unsupported construct", string(kind))
}

// compile prelude + user program as one top-level function (fn 0)
pn compile_program(user_src: string) Comp {
    let text: string = jq_prelude ++ "\n" ++ user_src
    var p: Parser = {src: text, codes: [for (i in 0 to len(text) - 1) ord(slice(text, i, i + 1))],
        pos: 0, n: len(text), failed: false, msg: ""}
    let ast = parse_pipe(p)
    skip_ws(p)
    if (not p.failed and p.pos != p.n) { parse_fail(p, "trailing input") }
    var cs: Comp = {code: [], consts: [], fn_entry: [], fn_nparams: [], fn_nlocals: [],
        ctx_parent: [], ctx_level: [], ctx_fn: [], ctx_scope: [], ctx_nlocals: [],
        sc_kind: [], sc_name: [], sc_arity: [], sc_index: [], sc_prev: [],
        failed: p.failed, msg: p.msg, synth: 0}
    if (cs.failed) { return cs }
    let top_fn: int = new_fn(cs, 0)
    let top: int = new_ctx(cs, -1, top_fn)
    cs.fn_entry[top_fn] = len(cs.code)
    compile(cs, top, ast, true)
    emit_op(cs, OP_RET)
    cs.fn_nlocals[top_fn] = cs.ctx_nlocals[top]
    cs
}

// ---------- the VM (jq_core.h) ----------

let FK_FORK = 1
let FK_TRY = 2
let FK_TRY_EXIT = 3
let FK_EACH = 4
let FK_RANGE = 5

// The forkable data stack, the fork records and the frame arena. Frames are
// ids into the fr_* arrays; their locals and closure parameters live in the
// flat `locals` and pfn/penv arrays at fr_locals/fr_params. `path` is null
// while no path(f) is being tracked.
type Vm = {stv: array, stp: int[], top: int, lim: int, high: int,
    fk_kind: int[], fk_pc: int[], fk_top: int[], fk_lim: int[], fk_fp: int[], fk_path: array,
    fk_vat: array, fk_subexp: int[], fk_aux: array, fk_aux2: array, fk_aux3: array,
    fk_auxi: int[], fk_active: bool[], fk_label: int[], nforks: int,
    fr_env: int[], fr_caller: int[], fr_retpc: int[], fr_fn: int[], fr_base: int[],
    fr_locals: int[], fr_params: int[], nframes: int, locals: array, pfn: int[], penv: int[],
    fp: int, path: array?, vat: any?, subexp: int, labels: int, outputs: array, err: any?,
    frame_limit: int}

pn st_push(var vm: Vm, v) {
    let idx: int = (if (vm.top > vm.lim) vm.top else vm.lim) + 1
    while (len(vm.stv) <= idx) {
        vm.stv.push(null)
        vm.stp.push(-1)
    }
    vm.stv[idx] = v
    vm.stp[idx] = vm.top
    vm.top = idx
    if (idx > vm.high) { vm.high = idx }
}

pn st_pop(var vm: Vm) {
    let v = vm.stv[vm.top]
    vm.top = vm.stp[vm.top]
    v
}

// push a fork record; every live stack cell is protected from later pushes
pn fork_push(var vm: Vm, kind: int, pc: int) int {
    let i: int = vm.nforks
    while (len(vm.fk_kind) <= i) {
        vm.fk_kind.push(0)
        vm.fk_pc.push(0)
        vm.fk_top.push(0)
        vm.fk_lim.push(0)
        vm.fk_fp.push(0)
        vm.fk_path.push(null)
        vm.fk_vat.push(null)
        vm.fk_subexp.push(0)
        vm.fk_aux.push(null)
        vm.fk_aux2.push(null)
        vm.fk_aux3.push(null)
        vm.fk_auxi.push(0)
        vm.fk_active.push(true)
        vm.fk_label.push(0)
    }
    vm.fk_kind[i] = kind
    vm.fk_pc[i] = pc
    vm.fk_top[i] = vm.top
    vm.fk_lim[i] = vm.lim
    vm.fk_fp[i] = vm.fp
    vm.fk_path[i] = vm.path
    vm.fk_vat[i] = vm.vat
    vm.fk_subexp[i] = vm.subexp
    vm.fk_aux[i] = null
    vm.fk_aux2[i] = null
    vm.fk_aux3[i] = null
    vm.fk_auxi[i] = 0
    vm.fk_active[i] = true
    vm.fk_label[i] = 0
    vm.nforks = i + 1
    // protect every live cell: later pushes go above them
    if (vm.top > vm.lim) { vm.lim = vm.top }
    i
}

pn fork_restore(var vm: Vm, i: int) {
    vm.top = vm.fk_top[i]
    vm.lim = vm.fk_lim[i]
    vm.high = if (vm.top > vm.lim) vm.top else vm.lim
    vm.fp = vm.fk_fp[i]
    vm.path = vm.fk_path[i]
    vm.vat = vm.fk_vat[i]
    vm.subexp = vm.fk_subexp[i]
}

pn frame_new(var vm: Vm, cs: Comp, fn_index: int, env: int, caller: int, retpc: int) int {
    let id: int = vm.nframes
    let base: int = len(vm.locals)
    var i: int = 0
    while (i < cs.fn_nlocals[fn_index]) {
        vm.locals.push(null)
        i = i + 1
    }
    let pbase: int = len(vm.pfn)
    var j: int = 0
    while (j < cs.fn_nparams[fn_index]) {
        vm.pfn.push(0)
        vm.penv.push(-1)
        j = j + 1
    }
    while (len(vm.fr_env) <= id) {
        vm.fr_env.push(-1)
        vm.fr_caller.push(-1)
        vm.fr_retpc.push(0)
        vm.fr_fn.push(0)
        vm.fr_base.push(0)
        vm.fr_locals.push(0)
        vm.fr_params.push(0)
    }
    vm.fr_env[id] = env
    vm.fr_caller[id] = caller
    vm.fr_retpc[id] = retpc
    vm.fr_fn[id] = fn_index
    vm.fr_base[id] = vm.nforks
    vm.fr_locals[id] = base
    vm.fr_params[id] = pbase
    vm.nframes = id + 1
    id
}

pn frame_hop(vm: Vm, f: int, hops: int) int {
    var x: int = f
    var h: int = hops
    while (h > 0) {
        x = vm.fr_env[x]
        h = h - 1
    }
    x
}

// Mark-compact over the frame arena: the frames reachable from the current
// frame and the fork records survive, renumbered in allocation order, with
// their locals and parameters. Runs at a call, before any frame id is taken.
pn vm_compact_frames(var vm: Vm) {
    let n: int = vm.nframes
    var marked: bool[] = []
    var k: int = 0
    while (k < n) {
        marked.push(false)
        k = k + 1
    }
    var work: int[] = [vm.fp]
    var f: int = 0
    while (f < vm.nforks) {
        work.push(vm.fk_fp[f])
        f = f + 1
    }
    // a work queue read by index: each marked frame enqueues its edges once
    var head: int = 0
    while (head < len(work)) {
        let id: int = work[head]
        head = head + 1
        if (id >= 0 and id < n and not marked[id]) {
            marked[id] = true
            work.push(vm.fr_env[id])
            work.push(vm.fr_caller[id])
            let pbase: int = vm.fr_params[id]
            let pend: int = if (id + 1 < n) vm.fr_params[id + 1] else len(vm.pfn)
            var q: int = pbase
            while (q < pend) {
                work.push(vm.penv[q])
                q = q + 1
            }
        }
    }
    var remap: int[] = []
    var live: int = 0
    var r: int = 0
    while (r < n) {
        remap.push(if (marked[r]) live else -1)
        if (marked[r]) { live = live + 1 }
        r = r + 1
    }
    var env2: int[] = []
    var caller2: int[] = []
    var retpc2: int[] = []
    var fn2: int[] = []
    var base2: int[] = []
    var locals_at2: int[] = []
    var params_at2: int[] = []
    var locals2: array = []
    var pfn2: int[] = []
    var penv2: int[] = []
    var id2: int = 0
    while (id2 < n) {
        if (marked[id2]) {
            env2.push(if (vm.fr_env[id2] >= 0) remap[vm.fr_env[id2]] else -1)
            caller2.push(if (vm.fr_caller[id2] >= 0) remap[vm.fr_caller[id2]] else -1)
            retpc2.push(vm.fr_retpc[id2])
            fn2.push(vm.fr_fn[id2])
            base2.push(vm.fr_base[id2])
            let lbase: int = vm.fr_locals[id2]
            let lend: int = if (id2 + 1 < n) vm.fr_locals[id2 + 1] else len(vm.locals)
            locals_at2.push(len(locals2))
            var a: int = lbase
            while (a < lend) {
                locals2.push(vm.locals[a])
                a = a + 1
            }
            let pbase: int = vm.fr_params[id2]
            let pend: int = if (id2 + 1 < n) vm.fr_params[id2 + 1] else len(vm.pfn)
            params_at2.push(len(pfn2))
            var b: int = pbase
            while (b < pend) {
                pfn2.push(vm.pfn[b])
                penv2.push(if (vm.penv[b] >= 0) remap[vm.penv[b]] else -1)
                b = b + 1
            }
        }
        id2 = id2 + 1
    }
    vm.fr_env = env2
    vm.fr_caller = caller2
    vm.fr_retpc = retpc2
    vm.fr_fn = fn2
    vm.fr_base = base2
    vm.fr_locals = locals_at2
    vm.fr_params = params_at2
    vm.locals = locals2
    vm.pfn = pfn2
    vm.penv = penv2
    vm.nframes = live
    vm.fp = if (vm.fp >= 0) remap[vm.fp] else -1
    var g: int = 0
    while (g < vm.nforks) {
        if (vm.fk_fp[g] >= 0) { vm.fk_fp[g] = remap[vm.fk_fp[g]] }
        g = g + 1
    }
    vm.frame_limit = if (live * 2 > 50000) live * 2 else 50000
}

fn is_label(v) bool => v is symbol and starts_with(string(v), "__jq_label_")

fn label_value(id: int) => symbol("__jq_label_" ++ string(id))

pn path_check(vm: Vm, t) bool {
    if (vm.path == null or vm.subexp != 0) { return true }
    // identity of the value at the current path, as jv_identical
    t == vm.vat and jtype(t) == jtype(vm.vat)
}

// element i of an iterated container; `keys` lists an object's keys once
pn each_produce(var vm: Vm, c, keys, i: int) {
    var key = null
    var val = null
    if (c is array) {
        key = i
        val = c[i]
    } else {
        key = keys[i]
        val = c[keys[i]]
    }
    if (vm.path != null and vm.subexp == 0) {
        vm.path = vm.path ++ [key]
        vm.vat = val
    }
    st_push(vm, val)
}

fn range_more(cur, upto, step) bool => if (step > 0) cur < upto else if (step < 0) cur > upto else false

// run the compiled program on `input`; outputs collect in vm.outputs.
// mode: 0 executes, 1 backtracks, 2 raises vm.err. Each case ends in
// `continue`, so the dispatch stops at its first match like a switch.
pn vm_run(cs: Comp, input) Vm {
    let code: int[] = cs.code
    var vm: Vm = {stv: [], stp: [], top: -1, lim: -1, high: -1,
        fk_kind: [], fk_pc: [], fk_top: [], fk_lim: [], fk_fp: [], fk_path: [], fk_vat: [],
        fk_subexp: [], fk_aux: [], fk_aux2: [], fk_aux3: [], fk_auxi: [], fk_active: [],
        fk_label: [], nforks: 0, fr_env: [], fr_caller: [], fr_retpc: [], fr_fn: [], fr_base: [],
        fr_locals: [], fr_params: [], nframes: 0, locals: [], pfn: [], penv: [],
        fp: -1, path: null, vat: null, subexp: 0, labels: 0, outputs: [], err: null,
        frame_limit: 50000}
    vm.fp = frame_new(vm, cs, 0, -1, -1, -1)
    st_push(vm, input)
    var pc: int = cs.fn_entry[0]
    var mode: int = 0
    while (true) {
        if (mode == 1) {
            // backtrack to the newest fork record that resumes
            if (vm.nforks == 0) { return vm }
            let i: int = vm.nforks - 1
            let kind: int = vm.fk_kind[i]
            if (kind == FK_FORK) {
                fork_restore(vm, i)
                pc = vm.fk_pc[i]
                vm.nforks = i
                mode = 0
                continue
            }
            if (kind == FK_EACH) {
                fork_restore(vm, i)
                let c = vm.fk_aux[i]
                let keys = vm.fk_aux2[i]
                let idx: int = vm.fk_auxi[i]
                vm.fk_auxi[i] = idx + 1
                pc = vm.fk_pc[i]
                if (idx + 1 >= len(c)) { vm.nforks = i }
                if (idx + 1 < len(c) and vm.top > vm.lim) { vm.lim = vm.top }
                each_produce(vm, c, keys, idx)
                mode = 0
                continue
            }
            if (kind == FK_RANGE) {
                fork_restore(vm, i)
                let cur = vm.fk_aux[i]
                let step = vm.fk_aux3[i]
                let next = cur + step
                pc = vm.fk_pc[i]
                let more = range_more(next, vm.fk_aux2[i], step)
                if (more) { vm.fk_aux[i] = next }
                if (more and vm.top > vm.lim) { vm.lim = vm.top }
                if (not more) { vm.nforks = i }
                st_push(vm, cur)
                mode = 0
                continue
            }
            // re-entering a try body reactivates it
            if (kind == FK_TRY_EXIT) { vm.fk_active[vm.fk_auxi[i]] = true }
            vm.nforks = i
            continue
        }
        if (mode == 2) {
            if (vm.nforks == 0) {
                print("jq: error (uncaught): " ++ json_text(vm.err) ++ "\n")
                return vm
            }
            let i: int = vm.nforks - 1
            vm.nforks = i
            if (vm.fk_kind[i] != FK_TRY or not vm.fk_active[i]) { continue }
            if (vm.fk_label[i] != 0) {
                // `label` catches only its own break, then backtracks
                if (is_label(vm.err) and vm.err == label_value(vm.fk_label[i])) {
                    fork_restore(vm, i)
                    mode = 1
                }
                continue
            }
            // a break passes through try
            if (is_label(vm.err)) { continue }
            fork_restore(vm, i)
            st_push(vm, vm.err)
            pc = vm.fk_pc[i]
            mode = 0
            continue
        }
        let op: int = code[pc]
        pc = pc + 1
        if (op == OP_DUP) {
            st_push(vm, vm.stv[vm.top])
            continue
        }
        if (op == OP_LOADK) {
            st_pop(vm)
            st_push(vm, cs.consts[code[pc]])
            pc = pc + 1
            continue
        }
        if (op == OP_SUBEXP_BEGIN) {
            st_push(vm, vm.stv[vm.top])
            vm.subexp = vm.subexp + 1
            continue
        }
        if (op == OP_SUBEXP_END) {
            let a = st_pop(vm)
            let b = st_pop(vm)
            st_push(vm, a)
            st_push(vm, b)
            vm.subexp = vm.subexp - 1
            continue
        }
        if (op == OP_INDEX or op == OP_INDEXK) {
            let t = st_pop(vm)
            var k = null
            if (op == OP_INDEXK) { k = cs.consts[code[pc]] }
            if (op == OP_INDEXK) { pc = pc + 1 }
            if (op == OP_INDEX) { k = st_pop(vm) }
            if (not path_check(vm, t)) {
                vm.err = "Invalid path expression with result " ++ describe(t)
                mode = 2
                continue
            }
            let r = index_value(t, k)
            if (not r.ok) {
                vm.err = r.value
                mode = 2
                continue
            }
            if (vm.path != null and vm.subexp == 0) {
                vm.path = vm.path ++ [k]
                vm.vat = r.value
            }
            st_push(vm, r.value)
            continue
        }
        if (op == OP_JUMP) {
            pc = code[pc]
            continue
        }
        if (op == OP_JUMP_F) {
            let cnd = st_pop(vm)
            pc = if (truthy(cnd)) pc + 1 else code[pc]
            continue
        }
        if (op == OP_JUMP_F_SUB) {
            let inp = st_pop(vm)
            let cnd = st_pop(vm)
            st_push(vm, inp)
            pc = if (truthy(cnd)) pc + 1 else code[pc]
            continue
        }
        if (op == OP_JUMP_F_KEEP) {
            pc = if (truthy(vm.stv[vm.top])) pc + 1 else code[pc]
            continue
        }
        if (op == OP_FORK) {
            fork_push(vm, FK_FORK, code[pc])
            pc = pc + 1
            continue
        }
        if (op == OP_BACKTRACK) {
            mode = 1
            continue
        }
        if ((op >= OP_STOREV and op <= OP_APPEND) or op == OP_LABEL_BEGIN or op == OP_BREAK) {
            let f: int = frame_hop(vm, vm.fp, code[pc])
            let slot: int = vm.fr_locals[f] + code[pc + 1]
            pc = pc + 2
            if (op == OP_STOREV) {
                vm.locals[slot] = st_pop(vm)
                continue
            }
            if (op == OP_STOREV_UNDER) {
                let inp = st_pop(vm)
                vm.locals[slot] = st_pop(vm)
                st_push(vm, inp)
                continue
            }
            if (op == OP_LOADV or op == OP_LOADVN) {
                st_pop(vm)
                st_push(vm, vm.locals[slot])
                if (op == OP_LOADVN) { vm.locals[slot] = null }
                continue
            }
            if (op == OP_APPEND) {
                // the collector array is private to this [..] until LOADVN
                vm.locals[slot].push(st_pop(vm))
                continue
            }
            if (op == OP_LABEL_BEGIN) {
                vm.labels = vm.labels + 1
                vm.locals[slot] = label_value(vm.labels)
                let at: int = fork_push(vm, FK_TRY, -1)
                vm.fk_label[at] = vm.labels
                continue
            }
            vm.err = vm.locals[slot]
            mode = 2
            continue
        }
        if (op == OP_EACH) {
            let c = st_pop(vm)
            let ct = jtype(c)
            if (ct != 5 and ct != 6) {
                vm.err = "Cannot iterate over " ++ describe(c)
                mode = 2
                continue
            }
            if (not path_check(vm, c)) {
                vm.err = "Invalid path expression with result " ++ describe(c)
                mode = 2
                continue
            }
            if (len(c) == 0) {
                mode = 1
                continue
            }
            let keys = if (ct == 6) [for (k at c) string(k)] else null
            if (len(c) > 1) {
                let at: int = fork_push(vm, FK_EACH, pc)
                vm.fk_aux[at] = c
                vm.fk_aux2[at] = keys
                vm.fk_auxi[at] = 1
            }
            each_produce(vm, c, keys, 0)
            continue
        }
        if (op == OP_CALL_JQ or op == OP_TAIL_CALL_JQ) {
            if (vm.nframes >= vm.frame_limit) { vm_compact_frames(vm) }
            let fn_index: int = code[pc]
            let hops: int = code[pc + 1]
            let nargs: int = code[pc + 2]
            var caller: int = vm.fp
            var retpc: int = pc + 3 + nargs
            // a tail call reuses the frame when no fork point refers into it
            if (op == OP_TAIL_CALL_JQ and vm.nforks == vm.fr_base[vm.fp] and vm.fr_caller[vm.fp] >= 0) {
                caller = vm.fr_caller[vm.fp]
                retpc = vm.fr_retpc[vm.fp]
            }
            let env: int = frame_hop(vm, vm.fp, hops)
            let site: int = vm.fp
            let f: int = frame_new(vm, cs, fn_index, env, caller, retpc)
            var i: int = 0
            while (i < nargs) {
                vm.pfn[vm.fr_params[f] + i] = code[pc + 3 + i]
                vm.penv[vm.fr_params[f] + i] = site
                i = i + 1
            }
            vm.fp = f
            pc = cs.fn_entry[fn_index]
            continue
        }
        if (op == OP_CALL_PARAM or op == OP_TAIL_CALL_PARAM) {
            if (vm.nframes >= vm.frame_limit) { vm_compact_frames(vm) }
            let owner: int = frame_hop(vm, vm.fp, code[pc])
            let slot: int = vm.fr_params[owner] + code[pc + 1]
            let cl_fn: int = vm.pfn[slot]
            let cl_env: int = vm.penv[slot]
            var caller: int = vm.fp
            var retpc: int = pc + 2
            if (op == OP_TAIL_CALL_PARAM and vm.nforks == vm.fr_base[vm.fp] and vm.fr_caller[vm.fp] >= 0) {
                caller = vm.fr_caller[vm.fp]
                retpc = vm.fr_retpc[vm.fp]
            }
            vm.fp = frame_new(vm, cs, cl_fn, cl_env, caller, retpc)
            pc = cs.fn_entry[cl_fn]
            continue
        }
        if (op == OP_RET) {
            if (vm.fr_caller[vm.fp] < 0) {
                // top level: an output of the program
                vm.outputs.push(st_pop(vm))
                mode = 1
                continue
            }
            pc = vm.fr_retpc[vm.fp]
            vm.fp = vm.fr_caller[vm.fp]
            continue
        }
        if (op == OP_CALL_NATIVE) {
            let id: int = code[pc]
            let nargs: int = code[pc + 1]
            pc = pc + 2
            let inp = st_pop(vm)
            var args: array = []
            var i: int = 0
            while (i < nargs) {
                args.push(null)
                i = i + 1
            }
            var j: int = nargs - 1
            while (j >= 0) {
                args[j] = st_pop(vm)
                j = j - 1
            }
            if (id == NAT_GETPATH and not path_check(vm, inp)) {
                vm.err = "Invalid path expression with result " ++ describe(inp)
                mode = 2
                continue
            }
            let r = call_native(id, inp, args)
            if (not r.ok) {
                vm.err = r.value
                mode = 2
                continue
            }
            if (id == NAT_GETPATH and vm.path != null and vm.subexp == 0) {
                vm.path = vm.path ++ args[0]
                vm.vat = r.value
            }
            st_push(vm, r.value)
            continue
        }
        if (op == OP_BINOP) {
            let bop: int = code[pc]
            pc = pc + 1
            st_pop(vm)
            let l = st_pop(vm)
            let r0 = st_pop(vm)
            let r = binop_value(bop, l, r0)
            if (not r.ok) {
                vm.err = r.value
                mode = 2
                continue
            }
            st_push(vm, r.value)
            continue
        }
        if (op == OP_TRY_BEGIN) {
            fork_push(vm, FK_TRY, code[pc])
            pc = pc + 1
            continue
        }
        if (op == OP_TRY_END) {
            // the body yielded: until backtracking re-enters it, its errors
            // are no longer this try's to catch
            var i: int = vm.nforks - 1
            while (i >= 0 and not (vm.fk_kind[i] == FK_TRY and vm.fk_active[i])) { i = i - 1 }
            if (i >= 0) {
                vm.fk_active[i] = false
                let at: int = fork_push(vm, FK_TRY_EXIT, -1)
                vm.fk_auxi[at] = i
            }
            continue
        }
        if (op == OP_POP) {
            st_pop(vm)
            continue
        }
        if (op == OP_NEWARR) {
            st_pop(vm)
            st_push(vm, [])
            continue
        }
        if (op == OP_OBJ_START) {
            let inp = st_pop(vm)
            st_push(vm, {})
            st_push(vm, inp)
            continue
        }
        if (op == OP_INSERT) {
            let inp = st_pop(vm)
            let val = st_pop(vm)
            let key = st_pop(vm)
            let obj = st_pop(vm)
            if (not (key is string)) {
                vm.err = "Object keys must be strings, not " ++ describe(key)
                mode = 2
                continue
            }
            st_push(vm, obj_with(obj, key, val))
            st_push(vm, inp)
            continue
        }
        if (op == OP_SLICE) {
            let t = st_pop(vm)
            let upper = st_pop(vm)
            let lower = st_pop(vm)
            if (vm.path != null and vm.subexp == 0) {
                vm.err = "jq-core does not support slice paths"
                mode = 2
                continue
            }
            let r = slice_value(t, lower, upper)
            if (not r.ok) {
                vm.err = r.value
                mode = 2
                continue
            }
            st_push(vm, r.value)
            continue
        }
        if (op == OP_RANGE) {
            let nargs: int = code[pc]
            pc = pc + 1
            st_pop(vm)
            let step = if (nargs == 3) st_pop(vm) else 1
            let upto = st_pop(vm)
            let from = st_pop(vm)
            if (jtype(from) != 3 or jtype(upto) != 3 or jtype(step) != 3) {
                vm.err = "Range bounds must be numeric"
                mode = 2
                continue
            }
            if (not range_more(from, upto, step)) {
                mode = 1
                continue
            }
            if (range_more(from + step, upto, step)) {
                let at: int = fork_push(vm, FK_RANGE, pc)
                vm.fk_aux[at] = from + step
                vm.fk_aux2[at] = upto
                vm.fk_aux3[at] = step
            }
            st_push(vm, from)
            continue
        }
        if (op == OP_PATH_BEGIN) {
            let v = st_pop(vm)
            st_push(vm, vm.path)
            st_push(vm, vm.vat)
            st_push(vm, vm.subexp)
            vm.path = []
            vm.vat = v
            vm.subexp = 0
            st_push(vm, v)
            continue
        }
        if (op == OP_PATH_END) {
            let r = st_pop(vm)
            if (not path_check(vm, r)) {
                vm.err = "Invalid path expression with result " ++ describe(r)
                mode = 2
                continue
            }
            let p = vm.path
            vm.subexp = st_pop(vm)
            vm.vat = st_pop(vm)
            vm.path = st_pop(vm)
            st_push(vm, p)
            continue
        }
        if (op == OP_NEG) {
            let v = st_pop(vm)
            if (jtype(v) != 3) {
                vm.err = describe(v) ++ " cannot be negated"
                mode = 2
                continue
            }
            st_push(vm, 0 - v)
            continue
        }
        if (op == OP_TOBOOL) {
            st_push(vm, truthy(st_pop(vm)))
            continue
        }
        print("jq-core: bad opcode " ++ string(op) ++ " at " ++ string(pc - 1) ++ "\n")
        return vm
    }
    vm
}

// ---------- harness ----------

pub let JQ_INPUT_NULL = 0
pub let JQ_INPUT_JSON = 1
pub let JQ_INPUT_RAW = 2

// run test/benchmark/text/jq/<name>.jq on the input and check its checksum
pub pn jq_vm_benchmark(name: string, input_kind: int, input_path: string, expected: int) {
    let filter: string = input("test/benchmark/text/jq/" ++ slice(name, 3) ++ ".jq", 'text')^
    let cs: Comp = compile_program(filter)
    if (cs.failed) {
        print(name ++ ": " ++ cs.msg ++ "\n")
        return null
    }
    var data = null
    if (input_kind == JQ_INPUT_JSON) { data = input(input_path, 'json')^ }
    if (input_kind == JQ_INPUT_RAW) { data = input(input_path, 'text')^ }
    // compiling the filter and loading the input stay outside the timed region
    let t0 = clock()
    let vm = vm_run(cs, data)
    let t1 = clock()
    if (len(vm.outputs) != 1) {
        print(name ++ ": FAIL expected one output, got " ++ string(len(vm.outputs)) ++ "\n")
    } else if (vm.outputs[0] == expected) {
        print(name ++ ": CHECKSUM:" ++ string(vm.outputs[0]) ++ "\n")
    } else {
        print(name ++ ": FAIL checksum=" ++ string(vm.outputs[0]) ++ "\n")
    }
    print("__TIMING__:" ++ string((t1 - t0) * 1000.0) ++ "\n")
}

// development harness: run a filter on a JSON input value, outputs as JSON lines
pub pn jq_vm_eval(filter: string, data) array {
    let cs: Comp = compile_program(filter)
    if (cs.failed) { return [cs.msg] }
    let vm = vm_run(cs, data)
    vm.outputs
}
