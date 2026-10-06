// Typed jq-semantics helpers for the jq_* text benchmarks' typed Lambda
// translations (test/benchmark/text/jq_*2.ls). Same computations as the
// untyped jq_query_common.ls, with annotations where jq's values are uniform
// (vibe/impl/Lambda_Impl_Jq_Tests.md §5.6).

pub let jq_modulus: int = 1000000007

// nth($d; 0 | recurse([., .])): both children share one value, as in jq
pub fn jq_tree(d: int) {
    if (d == 0) 0
    else {
        let child = jq_tree(d - 1);
        [child, child]
    }
}

// (.. | scalars) |= . + inc over a tree of int leaves
pub fn jq_bump_scalars(v, inc: int) {
    if (v is array) [for (x in v) jq_bump_scalars(x, inc)]
    else v + inc
}

// [paths] / [paths(type == "number")]: every non-empty path in pre-order
pub pn jq_collect_paths(v, prefix: array, numbers_only: bool, var out: array) {
    if (v is array) {
        var i: int = 0
        while (i < len(v)) {
            let path: array = prefix ++ [i]
            let child = v[i]
            if (not numbers_only or child is int or child is float) { out.push(path) }
            jq_collect_paths(child, path, numbers_only, out)
            i = i + 1
        }
        return null
    }
    if (v is map) {
        let keys: string[] = [for (k at v) string(k)]
        var j: int = 0
        while (j < len(keys)) {
            let path: array = prefix ++ [keys[j]]
            let child = v[keys[j]]
            if (not numbers_only or child is int or child is float) { out.push(path) }
            jq_collect_paths(child, path, numbers_only, out)
            j = j + 1
        }
    }
    return null
}

// flatten: the int leaves of nested arrays, in order
pub pn jq_flatten_into(v, var out: int[]) {
    if (v is array) {
        var i: int = 0
        while (i < len(v)) {
            jq_flatten_into(v[i], out)
            i = i + 1
        }
        return null
    }
    out.push(v)
    return null
}

// contains(b): jq's jv_contains, each search stopping at its first match
pub pn jq_contains(a, b) bool {
    if (a is array and b is array) {
        var j: int = 0
        while (j < len(b)) {
            var found: bool = false
            var i: int = 0
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
        let keys: string[] = [for (k at b) string(k)]
        var j: int = 0
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

// unique on strings: jq sorts, Lambda's unique keeps first-appearance order
pub fn jq_unique_strings(xs: string[]) string[] => unique(sort(xs))

// jq_mix's per-benchmark digest
pub fn jq_digest(v) int {
    if (v is array or v is map or v is string) len(v)
    else if (v is int) v
    else if (v == true) 1
    else 0
}

// range($from; $upto; $by) as jq defines it (empty when $by is 0)
pub pn jq_range(from: int, upto: int, by: int) int[] {
    var out: int[] = []
    var x: int = from
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

// .[from:to] bounds: jq clamps, counts negatives from the end, and treats
// null as an open bound
pub fn jq_slice_start(n: int, from: int?) int {
    let s0: int = if (from == null) 0 else if (from < 0) from + n else from
    if (s0 < 0) 0 else if (s0 > n) n else s0
}

pub fn jq_slice_end(n: int, start: int, to: int?) int {
    let e0: int = if (to == null) n else if (to < 0) to + n else to
    if (e0 > n) n else if (e0 < start) start else e0
}

pub fn jq_slice_string(s: string, from: int?, to: int?) string {
    let n: int = len(s)
    let start: int = jq_slice_start(n, from)
    slice(s, start, jq_slice_end(n, start, to)) or ""
}

pub fn jq_slice_array(v: array, from: int?, to: int?) array {
    let n: int = len(v)
    let start: int = jq_slice_start(n, from)
    slice(v, start, jq_slice_end(n, start, to)) or []
}

// a // b: b when a is null or false
pub fn jq_alt(a, b) => if (a == null or a == false) b else a

// .[i] on an array: null past the end
pub fn jq_at(arr: array, i: int) => if (i < len(arr)) arr[i] else null

// try tonumber catch default: jq parses a JSON number
pub fn jq_tonumber_or(text: string, default: int) => parse(text, 'json') ^ { default }

// explode | add
pub pn jq_codepoint_sum(text: string) int {
    var total: int = 0
    var i: int = 0
    while (i < len(text)) {
        total = total + ord(slice(text, i, i + 1))
        i = i + 1
    }
    total
}
