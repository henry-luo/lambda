// AWFY Benchmark: Permute
// Expected result: 8660
// Generates all permutations of an array via recursive swap

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn swap(v, i, j) {
    var tmp = v[i]
    v[i] = v[j]
    v[j] = tmp
}

pn permute(state, v, n) {
    state.count = state.count + 1
    if (n != 0) {
        var n1 = n - 1
        permute(state, v, n1)
        var i = n1
        while (i >= 0) {
            swap(v, n1, i)
            permute(state, v, n1)
            swap(v, n1, i)
            i = i - 1
        }
    }
}

pn benchmark() {
    let state = {count: 0}
    var v = make_array(6, 0)
    permute(state, v, 6)
    return state.count
}

pn main() {
    let result = benchmark()
    if (result == 8660) {
        print("Permute: PASS\n")
    } else {
        print("Permute: FAIL result=")
        print(result)
        print("\n")
    }
}
