// Kostya Benchmark: levenshtein (Typed version)
// Levenshtein edit distance using dynamic programming
// Adapted from github.com/kostya/benchmarks
// Computes edit distance between two strings, repeated for timing
// Expected: levenshtein("kitten", "sitting") = 3

// NOTE: no `string` return-type annotation on the string-building procedures —
// it currently segfaults the MIR JIT once the returned string is large enough to
// span a GC (repro: temp/repro_string_return_segv.ls). Param/local annotations
// and `int` return types are unaffected.
pn min2(a: int, b: int) int {
    if (a < b) { return a }
    return b
}

pn min3(a: int, b: int, c: int) int {
    return min2(min2(a, b), c)
}

pn levenshtein(s1: string, s2: string) int {
    let n: int = len(s1)
    let m: int = len(s2)

    // prev/curr stay unannotated: fill(n, int) already infers a packed ArrayNum,
    // and the row swap below rebinds them, so a bracket annotation would only
    // re-tag the vars as ANY
    // Use 2 rows instead of full matrix for space efficiency
    var prev = fill(m + 1, 0)
    var curr = fill(m + 1, 0)

    var j: int = 0
    while (j <= m) {
        prev[j] = j
        j = j + 1
    }

    var i: int = 1
    while (i <= n) {
        curr[0] = i
        j = 1
        while (j <= m) {
            var cost: int = 1
            if (s1[i - 1] == s2[j - 1]) {
                cost = 0
            }
            curr[j] = min3(
                prev[j] + 1,
                curr[j - 1] + 1,
                prev[j - 1] + cost
            )
            j = j + 1
        }
        // Swap rows
        var tmp = prev
        prev = curr
        curr = tmp
        i = i + 1
    }

    return prev[m]
}

pn make_string(ch: string, n: int) {
    var s: string = ch
    var sz: int = 1
    while (sz * 2 <= n) {
        s = s ++ s
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra: string = ch
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ ch
            esz = esz + 1
        }
        s = s ++ extra
    }
    return s
}

pn benchmark() {
    // Basic verification
    var d1: int = levenshtein("kitten", "sitting")
    var d2: int = levenshtein("saturday", "sunday")

    // Larger strings for benchmarking
    var s1: string = make_string("a", 500)
    var s2: string = make_string("b", 500)
    var d3: int = levenshtein(s1, s2)

    // Mix of edits
    var s3: string = make_string("ab", 200)
    var s4: string = make_string("ba", 200)
    var d4: int = levenshtein(s3, s4)

    print("levenshtein: d(kitten,sitting)=" ++ d1 ++ "\n")
    print("levenshtein: d(saturday,sunday)=" ++ d2 ++ "\n")
    print("levenshtein: d(aaa...,bbb...)=" ++ d3 ++ "\n")
    print("levenshtein: d(ababab...,babab...)=" ++ d4 ++ "\n")

    if (d1 == 3 and d2 == 3) {
        print("levenshtein: PASS\n")
    } else {
        print("levenshtein: FAIL\n")
    }
    return d1
}

pn main() {
    var __t0 = clock()
    benchmark()
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
