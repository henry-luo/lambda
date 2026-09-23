// v5 closes int at the exact int53 band: 2^70 saturates to shared `inf`.
// Every carrier must preserve that closure value through arithmetic, shaped
// fields, member assignment, and the packed element lane.

pn main() {
    // computed, because the literal band caps int SPELLINGS at 2^53-1
    let big = 4503599627370496 * 262144        // 2^70

    // arithmetic saturates at the int53 boundary
    print(string(big) ++ "\n")
    print((big * 2) ++ "\n")                   // remains inf

    // shaped storage: declared field, inferred map literal, element attribute
    let m: {v: int} = {v: big}
    print(string(m.v) ++ "\n")
    let m2 = {v: big}
    print(string(m2.v) ++ "\n")
    let e = <el v: big, "c">
    print(string(e.v) ++ "\n")

    // member assignment must write the carrier it reads
    var s = {count: big}
    s.count = s.count + 0
    print(string(s.count) ++ "\n")

    // packed element lane (D1)
    let arr = [big, 1]
    print(string(arr[0]) ++ "\n")

    // in-band values and div/mod truncation are unchanged
    print(string([7 + 5, 7 - 5, 7 * 5, 7 div 5, 7 % 5, -7 div 5, -7 % 5]) ++ "\n")
}
