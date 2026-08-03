// C16 gives `int` the float64-representable integers, so 2^70 is an ordinary
// int. Every carrier must hold it -- it only fits because `int`'s one native
// representation is the IEEE double (G0/G1). Each route below was separately
// clamped at 2^63 by a leftover int64_t carrier, and each is a distinct code
// path: boxed arithmetic, shaped field storage, member assignment, and the
// packed element lane.

pn main() {
    // computed, because the literal band caps int SPELLINGS at 2^53-1
    let big = 4503599627370496 * 262144        // 2^70

    // arithmetic: the boxed fallback computes in binary64, not int64
    print(big ++ "\n")
    print((big * 2) ++ "\n")                   // 2^71, not 2^64

    // shaped storage: declared field, inferred map literal, element attribute
    let m: {v: int} = {v: big}
    print(m.v ++ "\n")
    let m2 = {v: big}
    print(m2.v ++ "\n")
    let e = <el v: big; "c">
    print(e.v ++ "\n")

    // member assignment must write the carrier it reads
    var s = {count: big}
    s.count = s.count + 0
    print(s.count ++ "\n")

    // packed element lane (D1)
    let arr = [big, 1]
    print(arr[0] ++ "\n")

    // in-band values and div/mod truncation are unchanged
    print([7 + 5, 7 - 5, 7 * 5, 7 div 5, 7 % 5, -7 div 5, -7 % 5] ++ "\n")
}
