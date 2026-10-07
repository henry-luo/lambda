# Native Julia port of ../../jq_vm.ls: the same jq parser, compiler and VM.
# Evaluates the shared .jq filters; no C library or external interpreter is used.
const OP_DUP = 1
const OP_POP = 2
const OP_LOADK = 3
const OP_NEWARR = 4
const OP_OBJ_START = 5
const OP_SUBEXP_BEGIN = 6
const OP_SUBEXP_END = 7
const OP_INDEX = 8
const OP_INDEXK = 9
const OP_EACH = 10
const OP_SLICE = 11
const OP_FORK = 12
const OP_JUMP = 13
const OP_JUMP_F = 14
const OP_JUMP_F_SUB = 15
const OP_JUMP_F_KEEP = 16
const OP_BACKTRACK = 17
const OP_STOREV = 18
const OP_STOREV_UNDER = 19
const OP_LOADV = 20
const OP_LOADVN = 21
const OP_APPEND = 22
const OP_INSERT = 23
const OP_RANGE = 24
const OP_PATH_BEGIN = 25
const OP_PATH_END = 26
const OP_CALL_NATIVE = 27
const OP_CALL_JQ = 28
const OP_TAIL_CALL_JQ = 29
const OP_CALL_PARAM = 30
const OP_TAIL_CALL_PARAM = 31
const OP_RET = 32
const OP_TRY_BEGIN = 33
const OP_TRY_END = 34
const OP_LABEL_BEGIN = 35
const OP_BREAK = 36
const OP_BINOP = 37
const OP_NEG = 38
const OP_TOBOOL = 39
const OPB_ADD = 1
const OPB_SUB = 2
const OPB_MUL = 3
const OPB_DIV = 4
const OPB_MOD = 5
const OPB_EQ = 6
const OPB_NE = 7
const OPB_LT = 8
const OPB_LE = 9
const OPB_GT = 10
const OPB_GE = 11
const NAT_LENGTH = 1
const NAT_NOT = 2
const NAT_TYPE = 3
const NAT_KEYS = 4
const NAT_KEYS_UNSORTED = 5
const NAT_HAS = 6
const NAT_CONTAINS = 7
const NAT_TOSTRING = 8
const NAT_TOJSON = 9
const NAT_FROMJSON = 10
const NAT_TONUMBER = 11
const NAT_ASCII_UPCASE = 12
const NAT_ASCII_DOWNCASE = 13
const NAT_EXPLODE = 14
const NAT_IMPLODE = 15
const NAT_SPLIT = 16
const NAT_JOIN = 17
const NAT_ADD = 18
const NAT_FLATTEN = 19
const NAT_FLOOR = 20
const NAT_CEIL = 21
const NAT_MIN = 22
const NAT_MAX = 23
const NAT_UNIQUE = 24
const NAT_SORT = 25
const NAT_REVERSE = 26
const NAT_GETPATH = 27
const NAT_SETPATH = 28
const NAT_DELPATHS = 29
const NAT_FROM_ENTRIES = 30
const NAT_SORT_BY_IMPL = 31
const NAT_GROUP_BY_IMPL = 32
const NAT_ERROR0 = 33
const NAT_ERROR1 = 34
const native_names = String["length", "not", "type", "keys", "keys_unsorted", "has", "contains", "tostring", "tojson", "fromjson", "tonumber", "ascii_upcase", "ascii_downcase", "explode", "implode", "split", "join", "add", "flatten", "floor", "ceil", "min", "max", "unique", "sort", "reverse", "getpath", "setpath", "delpaths", "from_entries", "_sort_by_impl", "_group_by_impl", "error", "error"]
const native_arity = Int[0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 1, 1, 0, 1]
function jtype(v)
    ((v == nothing) ? 0 : ((v isa Bool) ? (v ? 2 : 1) : (((v isa Integer) || (v isa AbstractFloat)) ? 3 : ((v isa AbstractString) ? 4 : ((v isa AbstractVector) ? 5 : 6)))))
end

function type_name(v)
    t = jtype(v)
    ((t == 0) ? "null" : ((t <= 2) ? "boolean" : ((t == 3) ? "number" : ((t == 4) ? "string" : ((t == 5) ? "array" : "object")))))
end

function truthy(v)
    (!(v === nothing || v === false))
end

function sorted_keys(o)
    sort(Any[jstring(k) for k in Base.keys(o)])
end

# jq total order: null, false, true, number, string, array, object.
function jcmp(a, b)
    ta = jtype(a)
    tb = jtype(b)
    if (ta != tb)
        return ((ta < tb) ? (-1) : 1)
    end
    if (ta == 3)
        return ((a < b) ? (-1) : ((a > b) ? 1 : 0))
    end
    if (ta == 4)
        return ((a < b) ? (-1) : ((a > b) ? 1 : 0))
    end
    if (ta == 5)
        i = 0
        while ((i < length(a)) && (i < length(b)))
            c = jcmp(jget(a, i), jget(b, i))
            if (c != 0)
                return c
            end
            i = (i + 1)
        end
        return ((length(a) < length(b)) ? (-1) : ((length(a) > length(b)) ? 1 : 0))
    end
    if (ta == 6)
        ka = sorted_keys(a)
        kb = sorted_keys(b)
        ck = jcmp(ka, kb)
        if (ck != 0)
            return ck
        end
        j = 0
        while (j < length(ka))
            c = jcmp(jget(a, jget(ka, j)), jget(b, jget(ka, j)))
            if (c != 0)
                return c
            end
            j = (j + 1)
        end
        return 0
    end
    return 0
end

function jequal(a, b)
    (jcmp(a, b) == 0)
end

function jsort(xs)
    items = xs
    n = length(items)
    width = 1
    while (width < n)
        out = Any[]
        lo = 0
        while (lo < n)
            mid = (((lo + width) < n) ? (lo + width) : n)
            hi = (((lo + (2 * width)) < n) ? (lo + (2 * width)) : n)
            i = lo
            j = mid
            while ((i < mid) && (j < hi))
                if (jcmp(jget(items, j), jget(items, i)) < 0)
                    push!(out, jget(items, j))
                    j = (j + 1)
                else
                    push!(out, jget(items, i))
                    i = (i + 1)
                end
            end
            while (i < mid)
                push!(out, jget(items, i))
                i = (i + 1)
            end
            while (j < hi)
                push!(out, jget(items, j))
                j = (j + 1)
            end
            lo = (lo + (2 * width))
        end
        items = out
        width = (width * 2)
    end
    items
end

function jsort_indices(keys)
    n = length(keys)
    idx = Int[]
    k = 0
    while (k < n)
        push!(idx, k)
        k = (k + 1)
    end
    width = 1
    while (width < n)
        out = Int[]
        lo = 0
        while (lo < n)
            mid = (((lo + width) < n) ? (lo + width) : n)
            hi = (((lo + (2 * width)) < n) ? (lo + (2 * width)) : n)
            i = lo
            j = mid
            while ((i < mid) && (j < hi))
                if (jcmp(jget(keys, jget(idx, j)), jget(keys, jget(idx, i))) < 0)
                    push!(out, jget(idx, j))
                    j = (j + 1)
                else
                    push!(out, jget(idx, i))
                    i = (i + 1)
                end
            end
            while (i < mid)
                push!(out, jget(idx, i))
                i = (i + 1)
            end
            while (j < hi)
                push!(out, jget(idx, j))
                j = (j + 1)
            end
            lo = (lo + (2 * width))
        end
        idx = out
        width = (width * 2)
    end
    idx
end

# jq values are snapshots: copy only the object being updated.
function obj_with(o, key, v)
    r = copy(o)
    jset!(r, key, v)
    r
end

function obj_without(o, key)
    r = Dict{String,Any}()
    for (k, x) in Base.pairs(o)
        name = jstring(k)
        if (name != key)
            jset!(r, name, x)
        end
    end
    r
end

const jq_empty_object = Dict{String,Any}()
function copy_array(a)
    Any[x for x in a]
end

function hex4(c)
    digits = "0123456789abcdef"
    jcat("\\u00", jslice(digits, (c / 16), ((c / 16) + 1)), jslice(digits, (c % 16), ((c % 16) + 1)))
end

function json_string(s)
    parts = String["\""]
    i = 0
    while (i < length(s))
        ch = jor(()->jslice(s, i, (i + 1)), ()->"")
        c = jord(ch)
        push!(parts, ((c == 34) ? "\\\"" : ((c == 92) ? "\\\\" : ((c == 10) ? "\\n" : ((c == 9) ? "\\t" : ((c == 13) ? "\\r" : ((c == 8) ? "\\b" : ((c == 12) ? "\\f" : (((c < 32) || (c == 127)) ? hex4(c) : ch)))))))))
        i = (i + 1)
    end
    push!(parts, "\"")
    join(parts, "")
end

function json_text(v)
    if (v == nothing)
        return "null"
    end
    if (v isa Bool)
        return (v ? "true" : "false")
    end
    if ((v isa Integer) || (v isa AbstractFloat))
        return jstring(v)
    end
    if (v isa AbstractString)
        return json_string(v)
    end
    if (v isa AbstractVector)
        return jcat("[", join(Any[json_text(x) for x in v], ","), "]")
    end
    jcat("{", join(Any[jcat(json_string(jstring(k)), ":", json_text(x)) for (k, x) in Base.pairs(v)], ","), "}")
end

function tostring_value(v)
    if (v isa AbstractString)
        return v
    end
    json_text(v)
end

function describe(v)
    text = json_text(v)
    jcat(type_name(v), " (", ((length(text) > 11) ? jcat(jslice(text, 0, 10), "...") : text), ")")
end

function ok(v)
    JResult(true, v)
end

function fail(v)
    JResult(false, v)
end

function index_value(t, k)
    tt = jtype(t)
    kt = jtype(k)
    if ((tt == 0) && (((kt == 4) || (kt == 3)) || (kt == 0)))
        return ok(nothing)
    end
    if ((tt == 6) && (kt == 4))
        return ok(jget(t, k))
    end
    if ((tt == 5) && (kt == 3))
        d = floor(k)
        if (d < 0)
            d = (d + length(t))
        end
        if ((d < 0) || (d >= length(t)))
            return ok(nothing)
        end
        return ok(jget(t, jint(d)))
    end
    what = ((kt == 4) ? json_text(k) : type_name(k))
    fail(jcat("Cannot index ", type_name(t), " with ", what))
end

function slice_value(t, from, upto)
    tt = jtype(t)
    if (tt == 0)
        return ok(nothing)
    end
    if (((from != nothing) && (jtype(from) != 3)) || ((upto != nothing) && (jtype(upto) != 3)))
        return fail("Start and end indices of an array slice must be numbers")
    end
    if ((tt != 4) && (tt != 5))
        return fail(jcat(describe(t), " cannot be sliced"))
    end
    n = length(t)
    s = ((from == nothing) ? 0 : from)
    e = ((upto == nothing) ? n : upto)
    if (s < 0)
        s = (s + n)
    end
    if (e < 0)
        e = (e + n)
    end
    s = floor(s)
    e = ceil(e)
    if (s < 0)
        s = 0
    end
    if (e > n)
        e = n
    end
    if (s > n)
        s = n
    end
    if (e < s)
        e = s
    end
    ok(jor(()->jslice(t, jint(s), jint(e)), ()->((tt == 4) ? "" : Any[])))
end

function binop_value(op, l, r)
    lt = jtype(l)
    rt = jtype(r)
    if (op == OPB_ADD)
        if ((lt == 3) && (rt == 3))
            return ok((l + r))
        end
        if (lt == 0)
            return ok(r)
        end
        if (rt == 0)
            return ok(l)
        end
        if ((lt == 4) && (rt == 4))
            return ok(jcat(l, r))
        end
        if ((lt == 5) && (rt == 5))
            return ok(jcat(l, r))
        end
        if ((lt == 6) && (rt == 6))
            o = l
            for (k, x) in Base.pairs(r)
                o = obj_with(o, jstring(k), x)
            end
            return ok(o)
        end
        return fail(jcat(describe(l), " and ", describe(r), " cannot be added"))
    end
    if (op == OPB_SUB)
        if ((lt == 3) && (rt == 3))
            return ok((l - r))
        end
        if ((lt == 5) && (rt == 5))
            out = Any[]
            i = 0
            while (i < length(l))
                keep = true
                j = 0
                while ((j < length(r)) && keep)
                    if jequal(jget(l, i), jget(r, j))
                        keep = false
                    end
                    j = (j + 1)
                end
                if keep
                    push!(out, jget(l, i))
                end
                i = (i + 1)
            end
            return ok(out)
        end
        return fail(jcat(describe(l), " and ", describe(r), " cannot be subtracted"))
    end
    if (op == OPB_MUL)
        if ((lt == 3) && (rt == 3))
            return ok((l * r))
        end
        if (((lt == 4) && (rt == 3)) || ((lt == 3) && (rt == 4)))
            s = ((lt == 4) ? l : r)
            n = ((lt == 3) ? l : r)
            if (n <= 0)
                return ok(nothing)
            end
            parts = String[]
            i = 0
            while ((i < jint(n)) || (i == 0))
                push!(parts, s)
                i = (i + 1)
            end
            return ok(join(parts, ""))
        end
        return fail(jcat(describe(l), " and ", describe(r), " cannot be multiplied"))
    end
    if (op == OPB_DIV)
        if ((lt == 3) && (rt == 3))
            if (r == 0)
                return fail(jcat(describe(l), " and ", describe(r), " cannot be divided because the divisor is zero"))
            end
            return ok((l / r))
        end
        return fail(jcat(describe(l), " and ", describe(r), " cannot be divided"))
    end
    if (op == OPB_MOD)
        if ((lt == 3) && (rt == 3))
            a = jrescue(()->jint(l), ()->0)
            b = jrescue(()->jint(r), ()->0)
            if (b == 0)
                return fail(jcat(describe(l), " and ", describe(r), " cannot be divided because the divisor is zero"))
            end
            if (b < 0)
                b = (0 - b)
            end
            return ok((a % b))
        end
        return fail(jcat(describe(l), " and ", describe(r), " cannot be divided"))
    end
    c = jcmp(l, r)
    if (op == OPB_EQ)
        return ok((c == 0))
    end
    if (op == OPB_NE)
        return ok((c != 0))
    end
    if (op == OPB_LT)
        return ok((c < 0))
    end
    if (op == OPB_LE)
        return ok((c <= 0))
    end
    if (op == OPB_GT)
        return ok((c > 0))
    end
    ok((c >= 0))
end

function contains_value(a, b)
    ta = jtype(a)
    tb = jtype(b)
    if ((ta == 6) && (tb == 6))
        for (k, y) in Base.pairs(b)
            name = jstring(k)
            if ((jget(a, name) == nothing) && (!has_key(a, name)))
                return false
            end
            if (!contains_value(jget(a, name), y))
                return false
            end
        end
        return true
    end
    if ((ta == 5) && (tb == 5))
        j = 0
        while (j < length(b))
            found = false
            i = 0
            while ((i < length(a)) && (!found))
                found = contains_value(jget(a, i), jget(b, j))
                i = (i + 1)
            end
            if (!found)
                return false
            end
            j = (j + 1)
        end
        return true
    end
    if ((ta == 4) && (tb == 4))
        return occursin_reverse(a, b)
    end
    jequal(a, b)
end

function has_key(o, name)
    (length(Any[k for k in Base.keys(o) if (jstring(k) == name)]) > 0)
end

function getpath_value(t, p)
    cur = t
    i = 0
    while (i < length(p))
        if (cur == nothing)
            return ok(nothing)
        end
        r = index_value(cur, jget(p, i))
        if (!r.ok)
            return r
        end
        cur = r.value
        i = (i + 1)
    end
    ok(cur)
end

# path updates copy each container along the path, retaining untouched children.
function setpath_rec(t, p, i, v)
    if (i == length(p))
        return ok(v)
    end
    k = jget(p, i)
    if (k isa AbstractString)
        base = ((t == nothing) ? jq_empty_object : t)
        if (jtype(base) != 6)
            return index_value(base, k)
        end
        child = setpath_rec(jget(base, k), p, (i + 1), v)
        if (!child.ok)
            return child
        end
        return ok(obj_with(base, k, child.value))
    end
    if ((k isa Integer) || (k isa AbstractFloat))
        base = ((t == nothing) ? Any[] : t)
        if (jtype(base) != 5)
            bad = index_value(base, k)
            return (bad.ok ? fail(jcat("Cannot index ", type_name(base), " with number")) : bad)
        end
        d = floor(k)
        if (d < 0)
            d = (d + length(base))
        end
        if (d < 0)
            return fail("Out of bounds negative array index")
        end
        idx = jrescue(()->jint(d), ()->0)
        child = setpath_rec(((idx < length(base)) ? jget(base, idx) : nothing), p, (i + 1), v)
        if (!child.ok)
            return child
        end
        out = copy_array(base)
        while (length(out) <= idx)
            push!(out, nothing)
        end
        jset!(out, idx, child.value)
        return ok(out)
    end
    fail("Invalid path component")
end

function delpath_rec(t, p, i)
    if (t == nothing)
        return ok(nothing)
    end
    k = jget(p, i)
    is_last = (i == (length(p) - 1))
    if ((jtype(t) == 6) && (k isa AbstractString))
        if (!has_key(t, k))
            return ok(t)
        end
        if is_last
            return ok(obj_without(t, k))
        end
        child = delpath_rec(jget(t, k), p, (i + 1))
        if (!child.ok)
            return child
        end
        return ok(obj_with(t, k, child.value))
    end
    if ((jtype(t) == 5) && ((k isa Integer) || (k isa AbstractFloat)))
        d = floor(k)
        if (d < 0)
            d = (d + length(t))
        end
        if ((d < 0) || (d >= length(t)))
            return ok(t)
        end
        idx = jrescue(()->jint(d), ()->0)
        if is_last
            out = Any[]
            j = 0
            while (j < length(t))
                if (j != idx)
                    push!(out, jget(t, j))
                end
                j = (j + 1)
            end
            return ok(out)
        end
        child = delpath_rec(jget(t, idx), p, (i + 1))
        if (!child.ok)
            return child
        end
        out2 = copy_array(t)
        jset!(out2, idx, child.value)
        return ok(out2)
    end
    index_value(t, k)
end

function delpaths_value(t, ps)
    if (jtype(ps) != 5)
        return fail("Paths must be specified as an array")
    end
    sorted = jsort(ps)
    cur = t
    i = (length(sorted) - 1)
    while (i >= 0)
        p = jget(sorted, i)
        if (jtype(p) != 5)
            return fail("Path must be specified as an array")
        end
        if (length(p) == 0)
            return ok(nothing)
        end
        r = delpath_rec(cur, p, 0)
        if (!r.ok)
            return r
        end
        cur = r.value
        i = (i - 1)
    end
    ok(cur)
end

function flatten_into(a, out)
    i = 0
    while (i < length(a))
        if (jget(a, i) isa AbstractVector)
            flatten_into(jget(a, i), out)
        else
            push!(out, jget(a, i))
        end
        i = (i + 1)
    end
end

function codepoints(s)
    out = Int[]
    i = 0
    while (i < length(s))
        push!(out, jord(jslice(s, i, (i + 1))))
        i = (i + 1)
    end
    out
end

# native jq operations; generator control stays in the bytecode VM.
function call_native(id, input, args)
    t = jtype(input)
    if (id == NAT_LENGTH)
        if (t == 0)
            return ok(0)
        end
        if (t == 3)
            return ok(abs(input))
        end
        if (((t == 4) || (t == 5)) || (t == 6))
            return ok(length(input))
        end
        return fail(jcat(describe(input), " has no length"))
    end
    if (id == NAT_NOT)
        return ok((!truthy(input)))
    end
    if (id == NAT_TYPE)
        return ok(type_name(input))
    end
    if ((id == NAT_KEYS) || (id == NAT_KEYS_UNSORTED))
        if (t == 6)
            return ok(((id == NAT_KEYS) ? sorted_keys(input) : Any[jstring(k) for k in Base.keys(input)]))
        end
        if (t == 5)
            idx = Int[]
            i = 0
            while (i < length(input))
                push!(idx, i)
                i = (i + 1)
            end
            return ok(idx)
        end
        return fail(jcat(describe(input), " has no keys"))
    end
    if (id == NAT_HAS)
        if ((t == 6) && (jget(args, 0) isa AbstractString))
            return ok(has_key(input, jget(args, 0)))
        end
        if ((t == 5) && (jtype(jget(args, 0)) == 3))
            return ok(((jget(args, 0) >= 0) && (jget(args, 0) < length(input))))
        end
        return fail(jcat(describe(input), ", ", describe(jget(args, 0)), ": cannot check whether one has a key"))
    end
    if (id == NAT_CONTAINS)
        tb = jtype(jget(args, 0))
        bools = (((t == 1) || (t == 2)) && ((tb == 1) || (tb == 2)))
        if ((t != tb) && (!bools))
            return fail(jcat(describe(input), " and ", describe(jget(args, 0)), " cannot have their containment checked"))
        end
        return ok(contains_value(input, jget(args, 0)))
    end
    if (id == NAT_TOSTRING)
        return ok(tostring_value(input))
    end
    if (id == NAT_TOJSON)
        return ok(json_text(input))
    end
    if (id == NAT_FROMJSON)
        if (t != 4)
            return fail(jcat(describe(input), " cannot be parsed as JSON"))
        end
        try
            return ok(jparse(input, :json))
        catch
            return fail(jcat("Invalid JSON text: ", input))
        end
    end
    if (id == NAT_TONUMBER)
        if (t == 3)
            return ok(input)
        end
        if (t == 4)
            n = jrescue(()->jparse(input, Symbol("json")), ()->nothing)
            if jtype(n) == 3
                return ok(n)
            end
            return fail(jcat("Cannot parse '", input, "' as JSON"))
        end
        return fail(jcat(describe(input), " cannot be parsed as a number"))
    end
    if ((id == NAT_ASCII_UPCASE) || (id == NAT_ASCII_DOWNCASE))
        if (t != 4)
            return fail(jcat(describe(input), " cannot be case-converted"))
        end
        cps = codepoints(input)
        mapped = Any[jchr(((((id == NAT_ASCII_UPCASE) && (c >= 97)) && (c <= 122)) ? (c - 32) : ((((id == NAT_ASCII_DOWNCASE) && (c >= 65)) && (c <= 90)) ? (c + 32) : c))) for c in cps]
        return ok(join(mapped, ""))
    end
    if (id == NAT_EXPLODE)
        if (t != 4)
            return fail(jcat(describe(input), " cannot be exploded"))
        end
        return ok(codepoints(input))
    end
    if (id == NAT_IMPLODE)
        if (t != 5)
            return fail(jcat(describe(input), " cannot be imploded"))
        end
        return ok(join(Any[jchr(jint(c)) for c in input], ""))
    end
    if (id == NAT_SPLIT)
        if ((t != 4) || (!(jget(args, 0) isa AbstractString)))
            return fail("split input and separator must be strings")
        end
        if (length(input) == 0)
            return ok(Any[])
        end
        return ok(split(input, jget(args, 0)))
    end
    if (id == NAT_JOIN)
        if (t != 5)
            return fail(jcat(describe(input), " cannot be joined"))
        end
        sep = ((jget(args, 0) isa AbstractString) ? jget(args, 0) : "")
        return ok(join(Any[((x == nothing) ? "" : tostring_value(x)) for x in input], sep))
    end
    if (id == NAT_ADD)
        if (t == 0)
            return ok(nothing)
        end
        if (t != 5)
            return fail(jcat(describe(input), " cannot be added up"))
        end
        if (length(input) == 0)
            return ok(nothing)
        end
        acc = jget(input, 0)
        fresh_array = Any[]
        fresh_object = Dict{String,Any}()
        mode = 0
        i = 1
        while (i < length(input))
            x = jget(input, i)
            at = ((mode == 1) ? 5 : ((mode == 2) ? 6 : jtype(acc)))
            xt = jtype(x)
            if ((at == 5) && (xt == 5))
                if (mode != 1)
                    fresh_array = copy_array(acc)
                    mode = 1
                end
                j = 0
                while (j < length(x))
                    push!(fresh_array, jget(x, j))
                    j = (j + 1)
                end
            elseif ((at == 6) && (xt == 6))
                if (mode != 2)
                    fresh_object = Dict{String,Any}()
                    for (k0, y0) in Base.pairs(acc)
                        jset!(fresh_object, jstring(k0), y0)
                    end
                    mode = 2
                end
                for (k, y) in Base.pairs(x)
                    jset!(fresh_object, jstring(k), y)
                end
            else
                if (mode == 1)
                    acc = fresh_array
                end
                if (mode == 2)
                    acc = fresh_object
                end
                mode = 0
                r = binop_value(OPB_ADD, acc, x)
                if (!r.ok)
                    return r
                end
                acc = r.value
            end
            i = (i + 1)
        end
        return ok(((mode == 1) ? fresh_array : ((mode == 2) ? fresh_object : acc)))
    end
    if (id == NAT_FLATTEN)
        if (t != 5)
            return fail(jcat(describe(input), " cannot be flattened"))
        end
        out = Any[]
        flatten_into(input, out)
        return ok(out)
    end
    if ((id == NAT_FLOOR) || (id == NAT_CEIL))
        if (t != 3)
            return fail(jcat(describe(input), " number required"))
        end
        return ok(((id == NAT_FLOOR) ? floor(input) : ceil(input)))
    end
    if ((id == NAT_MIN) || (id == NAT_MAX))
        if (t != 5)
            return fail(jcat(describe(input), " cannot be iterated"))
        end
        if (length(input) == 0)
            return ok(nothing)
        end
        best = jget(input, 0)
        i = 1
        while (i < length(input))
            c = jcmp(jget(input, i), best)
            if ((id == NAT_MIN) ? (c < 0) : (c >= 0))
                best = jget(input, i)
            end
            i = (i + 1)
        end
        return ok(best)
    end
    if ((id == NAT_SORT) || (id == NAT_UNIQUE))
        if (t != 5)
            return fail(jcat(describe(input), " cannot be sorted, as it is not an array"))
        end
        sorted = jsort(input)
        if ((id == NAT_SORT) || (length(sorted) < 2))
            return ok(sorted)
        end
        out = Any[jget(sorted, 0)]
        i = 1
        while (i < length(sorted))
            if (!jequal(jget(sorted, i), jget(out, (length(out) - 1))))
                push!(out, jget(sorted, i))
            end
            i = (i + 1)
        end
        return ok(out)
    end
    if (id == NAT_REVERSE)
        if (t == 0)
            return ok(Any[])
        end
        if (t == 4)
            return ok(join(reverse(Any[jchr(c) for c in codepoints(input)]), ""))
        end
        if (t != 5)
            return fail(jcat(describe(input), " cannot be reversed"))
        end
        return ok(reverse(input))
    end
    if (id == NAT_GETPATH)
        if (jtype(jget(args, 0)) != 5)
            return fail("Path must be specified as an array")
        end
        return getpath_value(input, jget(args, 0))
    end
    if (id == NAT_SETPATH)
        if (jtype(jget(args, 0)) != 5)
            return fail("Path must be specified as an array")
        end
        return setpath_rec(input, jget(args, 0), 0, jget(args, 1))
    end
    if (id == NAT_DELPATHS)
        return delpaths_value(input, jget(args, 0))
    end
    if (id == NAT_FROM_ENTRIES)
        if (t != 5)
            return fail(jcat(describe(input), " cannot be iterated"))
        end
        r = Dict{String,Any}()
        names = String["key", "k", "name", "Name", "K", "Key"]
        i = 0
        while (i < length(input))
            e = jget(input, i)
            if (jtype(e) != 6)
                return fail(jcat(describe(e), " cannot be indexed as an entry"))
            end
            key = nothing
            n = 0
            while ((n < 6) && (!truthy(key)))
                key = jget(e, jget(names, n))
                n = (n + 1)
            end
            name = ((key isa AbstractString) ? key : json_text(key))
            jset!(r, name, (has_key(e, "value") ? jget(e, "value") : jget(e, "v")))
            i = (i + 1)
        end
        return ok(r)
    end
    if ((id == NAT_SORT_BY_IMPL) || (id == NAT_GROUP_BY_IMPL))
        if (((t != 5) || (jtype(jget(args, 0)) != 5)) || (length(jget(args, 0)) != length(input)))
            return fail(jcat(describe(input), " cannot be sorted, as it is not an array"))
        end
        keys = jget(args, 0)
        order = jsort_indices(keys)
        if (id == NAT_SORT_BY_IMPL)
            return ok(Any[jget(input, i) for i in order])
        end
        groups = Any[]
        group = Any[]
        i = 0
        while (i < length(order))
            if ((i > 0) && (!jequal(jget(keys, jget(order, i)), jget(keys, jget(order, (i - 1))))))
                push!(groups, group)
                group = Any[]
            end
            push!(group, jget(input, jget(order, i)))
            i = (i + 1)
        end
        if (length(order) > 0)
            push!(groups, group)
        end
        return ok(groups)
    end
    if (id == NAT_ERROR0)
        return fail(input)
    end
    if (id == NAT_ERROR1)
        return fail(jget(args, 0))
    end
    fail("unknown native")
end
