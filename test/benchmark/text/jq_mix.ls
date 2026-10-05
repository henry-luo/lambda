// Text benchmark jq_mix: jq/mix.jq written as a Lambda query: the 27 jaq
// bench filters, each reduced to jq_digest, summed per round
// (vibe/impl/Lambda_Impl_Jq_Tests.md §5.6). Generators become bounded loops
// that stop where jq's limit/until/recursion stops; jq's in-place appends
// on refcount-1 values become pushes.
import ~~.jq_query_common

let rounds = 7

// def upto($max): if . < $max then ., (.+1 | upto($max)) end
pn upto(i, max, var out) {
    out.push(i)
    if (i < max) { upto(i + 1, max, out) }
    return null
}

// def rec: if . < $max then ., (.+1 | rec), . end
pn pyramid(i, max, var out) {
    out.push(i)
    if (i < max) {
        pyramid(i + 1, max, out)
        out.push(i)
    }
    return null
}

// def ack($m; $n)
fn ack(m, n) {
    if (m == 0) n + 1
    else if (n == 0) ack(m - 1, 1)
    else ack(m - 1, ack(m, n - 1))
}

// try error catch .: the error's payload comes back as the caught value
fn raise_value(x) int^ {
    raise error(string(x))
}

// "a" * n
pn repeat_string(s, n) {
    var parts = []
    var i = 0
    while (i < n) {
        parts.push(s)
        i = i + 1
    }
    join(parts, "")
}

// [foreach range(n) as $x (0; . + $x; extract)]: extract `.` or `$x, .`
pn cumsum(n, both) {
    var acc = 0
    var out = []
    var x = 0
    while (x < n) {
        acc = acc + x
        if (both) { out.push(x) }
        out.push(acc)
        x = x + 1
    }
    out
}

// [range(.) | {(tostring): .}] | add
pn kv_object(n) {
    let singles = [for (x in jq_range(0, n, 1)) {[string(x)]: x}]
    var merged = {}
    var i = 0
    while (i < len(singles)) {
        for (k, v at singles[i]) { merged[string(k)] = v }
        i = i + 1
    }
    merged
}

// .[] += 1 over an object
pn object_add_one(m) {
    var r = {}
    for (k, v at m) { r[string(k)] = v + 1 }
    r
}

// with_entries(.value += 1): to_entries | map(...) | from_entries
pn entries_add_one(m) {
    let entries = [for (k, v at m) {key: string(k), value: v + 1}]
    var r = {}
    var i = 0
    while (i < len(entries)) {
        r[entries[i].key] = entries[i].value
        i = i + 1
    }
    r
}

// [range(.) | [.]] | add
pn add_singletons(n) {
    let singles = [for (x in jq_range(0, n, 1)) [x]]
    var out = []
    var i = 0
    while (i < len(singles)) {
        var j = 0
        while (j < len(singles[i])) {
            out.push(singles[i][j])
            j = j + 1
        }
        i = i + 1
    }
    out
}

// [limit(n; repeat(x))]
pn repeat_limit(x, n) {
    var out = []
    while (len(out) < n) { out.push(x) }
    out
}

// [limit(n; 0 | recurse(. + 1))]
pn recurse_limit(n) {
    var out = []
    var x = 0
    while (len(out) < n) {
        out.push(x)
        x = x + 1
    }
    out
}

// [limit(.; repeat("a"))] | add | explode | implode
pn ex_implode(n) {
    let text = join(repeat_limit("a", n), "")
    let codes = [for (i in jq_range(0, len(text), 1)) ord(slice(text, i, i + 1))]
    join([for (cp in codes) chr(cp)], "")
}

// [{from: 1, upto: range(-.; .), by: range(-.; .) | select(. != 0)}
//   | ([range(.from; .upto; .by)] | length) == ([(.upto - .from) / .by | ceil, 0] | max)]
//   | map(select(.)) | length
pn range_prop(n) {
    var checks = []
    for (upto in jq_range(0 - n, n, 1)) {
        for (by in jq_range(0 - n, n, 1)) {
            if (by != 0) {
                let o = {from: 1, upto: upto, by: by}
                checks.push(len(jq_range(o.from, o.upto, o.by)) == max([ceil((o.upto - o.from) / o.by), 0]))
            }
        }
    }
    len([for (ok in checks where ok) ok])
}

// "a" * . | [range(length) as $x | .[$x:], .[:-$x]]
pn str_slice(n) {
    let s = repeat_string("a", n)
    var out = []
    var x = 0
    while (x < len(s)) {
        out.push(jq_slice(s, x, null))
        out.push(jq_slice(s, null, 0 - x))
        x = x + 1
    }
    out
}

pn mix_digest() {
    var results = []
    var a = []
    upto(0, 512, a)
    results.push(a)
    var acc = [[]]
    for (x in jq_range(0, 2048, 1)) { acc[0].push(x) }
    results.push(acc)
    results.push(reverse(jq_range(0, 65536, 1)))
    results.push(sort([for (x in jq_range(0, 65536, 1)) 0 - x]))
    results.push([for (x in jq_range(0, 65536, 1) group by (x % 2) as k into g order by g.k) [*content(g)]])
    let mm = jq_range(0, 65536, 1)
    results.push(min(mm) + max(mm))
    results.push(add_singletons(8192))
    results.push(kv_object(2048))
    results.push(object_add_one(kv_object(2048)))
    results.push(entries_add_one(kv_object(2048)))
    results.push(ex_implode(1024))
    var total = 0
    for (x in jq_range(0, 65536, 1)) { total = total + x }
    results.push(total)
    results.push([for (x in jq_range(0, 65536, 1)) raise_value(x) ^ { int(^.message) ^ { 0 } }])
    results.push(repeat_limit(1, 1024))
    results.push(recurse_limit(1024))
    let lastv = jq_range(0, 65536, 1)
    results.push(lastv[len(lastv) - 1])
    var p = []
    pyramid(0, 512, p)
    results.push(len(p))
    let t = jq_tree(12)
    results.push([jq_contains(t, t)])
    var leaves = []
    jq_flatten_into(t, leaves)
    results.push(leaves)
    results.push(jq_bump_scalars(t, 1))
    var paths = []
    jq_collect_paths(t, [], false, paths)
    results.push(paths)
    let json_text = "[" ++ join([for (x in jq_range(0, 4096, 1)) format(x, 'json')], ",") ++ "]"
    results.push(parse(json_text, 'json') ^ { [] })
    results.push(ack(3, 5))
    results.push(range_prop(24))
    results.push(cumsum(65536, false))
    results.push(cumsum(65536, true))
    results.push(str_slice(1024))
    sum([for (v in results) jq_digest(v)])
}

pn main() {
    let t0 = clock()
    var checksum = 0
    var r = 0
    while (r < rounds) {
        checksum = (checksum * 31 + mix_digest() + r) % jq_modulus
        r = r + 1
    }
    let t1 = clock()
    if (checksum == 98172625) {
        print("jq_mix: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("jq_mix: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((t1 - t0) * 1000.0) ++ "\n")
}
