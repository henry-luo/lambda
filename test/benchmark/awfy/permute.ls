// AWFY Benchmark: Permute
// Expected result: 8660
// Generates all permutations of an array via recursive swap

pn swap(v, i, j) {
    var tmp = v[i]
    v[i] = v[j]
    v[j] = tmp
}

pn permute(st, v, n) {
    st.count = st.count + 1
    if (n != 0) {
        var n1 = n - 1
        permute(st, v, n1)
        var i = n1
        while (i >= 0) {
            swap(v, n1, i)
            permute(st, v, n1)
            swap(v, n1, i)
            i = i - 1
        }
    }
}

pn benchmark() {
    let st = {count: 0}
    var v = fill(6, 0)
    permute(st, v, 6)
    return st.count
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 8660) {
        print("Permute: PASS\n")
    } else {
        print("Permute: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
