// jq semantics shared by the jq_* text benchmarks' Lambda translations
// (test/benchmark/text/jq_*.ls). Each translation runs the same computation
// as its jq/*.jq filter; these helpers spell out where jq's rules differ from
// Lambda's defaults (vibe/impl/Lambda_Impl_Jq_Tests.md §5.6).

pub let jq_modulus = 1000000007

// nth($d; 0 | recurse([., .])): the complete binary tree of depth d,
// both children sharing one value, as jq builds it
pub fn jq_tree(d) {
    if (d == 0) 0
    else {
        let child = jq_tree(d - 1);
        [child, child]
    }
}

// (.. | scalars) |= . + inc: rebuild every container, adding inc to each scalar
pub fn jq_bump_scalars(v, inc) {
    if (v is array) [for (x in v) jq_bump_scalars(x, inc)]
    else v + inc
}

// [paths] / [paths(type == "number")]: every non-empty path in pre-order,
// each a fresh array as jq yields it. Statement-form loops: the walk
// collects nothing but `out`.
pub pn jq_collect_paths(v, prefix, numbers_only, var out) {
    if (v is array) {
        var i = 0
        while (i < len(v)) {
            let path = prefix ++ [i]
            let child = v[i]
            if (not numbers_only or child is int or child is float) { out.push(path) }
            jq_collect_paths(child, path, numbers_only, out)
            i = i + 1
        }
        return null
    }
    if (v is map) {
        let keys = [for (k at v) string(k)]
        var j = 0
        while (j < len(keys)) {
            let path = prefix ++ [keys[j]]
            let child = v[keys[j]]
            if (not numbers_only or child is int or child is float) { out.push(path) }
            jq_collect_paths(child, path, numbers_only, out)
            j = j + 1
        }
    }
    return null
}

// flatten: the leaves of nested arrays, in order
pub pn jq_flatten_into(v, var out) {
    if (v is array) {
        var i = 0
        while (i < len(v)) {
            jq_flatten_into(v[i], out)
            i = i + 1
        }
        return null
    }
    out.push(v)
    return null
}

// contains(b): jq's jv_contains. Arrays: every element of b is contained by
// some element of a, each search stopping at its first match; objects: every
// key of b is in a and contained; a type mismatch below the top level is false.
pub pn jq_contains(a, b) {
    if (a is array and b is array) {
        var j = 0
        while (j < len(b)) {
            var found = false
            var i = 0
            while (i < len(a) and not found) {
                found = jq_contains(a[i], b[j])
                i = i + 1
            }
            if (not found) { return false }
            j = j + 1
        }
        return true
    }
    if (a is map and b is map) {
        let keys = [for (k at b) string(k)]
        var j = 0
        while (j < len(keys)) {
            if (a[keys[j]] == null) { return false }
            if (not jq_contains(a[keys[j]], b[keys[j]])) { return false }
            j = j + 1
        }
        return true
    }
    if (a is string and b is string) { return contains(a, b) }
    return a == b
}

// unique: jq sorts, Lambda's unique keeps first-appearance order
pub fn jq_unique(xs) => unique(sort(xs))

// jq_mix's per-benchmark digest
pub fn jq_digest(v) {
    if (v is array or v is map or v is string) len(v)
    else if (v is int or v is float) v
    else if (v == true) 1
    else 0
}

// range($from; $upto; $by) as jq defines it (empty when $by is 0)
pub pn jq_range(from, upto, by) {
    var out = []
    var x = from
    if (by > 0) {
        while (x < upto) {
            out.push(x)
            x = x + by
        }
    } else if (by < 0) {
        while (x > upto) {
            out.push(x)
            x = x + by
        }
    }
    out
}

// .[from:to] on a string or array: jq clamps, counts negatives from the end,
// and treats null as an open bound
pub fn jq_slice(v, from, to) {
    let n = len(v)
    let s0 = if (from == null) 0 else if (from < 0) from + n else from
    let e0 = if (to == null) n else if (to < 0) to + n else to
    let s = if (s0 < 0) 0 else if (s0 > n) n else s0
    let e = if (e0 > n) n else if (e0 < s) s else e0
    slice(v, s, e)
}

// a // b: b when a is null or false (jq's "" and 0 are truthy, unlike `or`)
pub fn jq_alt(a, b) => if (a == null or a == false) b else a

// .[i] on an array: null past the end
pub fn jq_at(arr, i) => if (i < len(arr)) arr[i] else null

// tonumber, as `try tonumber catch default`: jq parses a JSON number
pub fn jq_tonumber_or(text, default) => parse(text, 'json') ^ { default }

// explode | add: the sum of a string's code points
pub pn jq_codepoint_sum(text) {
    var total = 0
    var i = 0
    while (i < len(text)) {
        total = total + ord(slice(text, i, i + 1))
        i = i + 1
    }
    total
}
