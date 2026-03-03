// Kostya Benchmark: levenshtein
// Levenshtein edit distance using dynamic programming
// Adapted from github.com/kostya/benchmarks
// Computes edit distance between two strings, repeated for timing
// Expected: levenshtein("kitten", "sitting") = 3

pn min2(a, b) {
    if (a < b) { return a }
    return b
}

pn min3(a, b, c) {
    return min2(min2(a, b), c)
}

pn levenshtein(s1, s2) {
    let n = len(s1)
    let m = len(s2)

    // Use 2 rows instead of full matrix for space efficiency
    var prev = fill(m + 1, 0)
    var curr = fill(m + 1, 0)

    var j = 0
    while (j <= m) {
        prev[j] = j
        j = j + 1
    }

    var i = 1
    while (i <= n) {
        curr[0] = i
        j = 1
        while (j <= m) {
            var cost = 1
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

pn make_string(ch, n) {
    var s = ch
    var sz = 1
    while (sz * 2 <= n) {
        s = s ++ s
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = ch
        var esz = 1
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
    var d1 = levenshtein("kitten", "sitting")
    var d2 = levenshtein("saturday", "sunday")

    // Larger strings for benchmarking
    var s1 = make_string("a", 500)
    var s2 = make_string("b", 500)
    var d3 = levenshtein(s1, s2)

    // Mix of edits
    var s3 = make_string("ab", 200)
    var s4 = make_string("ba", 200)
    var d4 = levenshtein(s3, s4)

    print("levenshtein: d(kitten,sitting)=" ++ string(d1) ++ "\n")
    print("levenshtein: d(saturday,sunday)=" ++ string(d2) ++ "\n")
    print("levenshtein: d(aaa...,bbb...)=" ++ string(d3) ++ "\n")
    print("levenshtein: d(ababab...,babab...)=" ++ string(d4) ++ "\n")

    if (d1 == 3 and d2 == 3) {
        print("levenshtein: PASS\n")
    } else {
        print("levenshtein: FAIL\n")
    }
    return d1
}

pn main() {
    benchmark()
}
