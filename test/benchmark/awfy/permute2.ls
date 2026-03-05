// AWFY Benchmark: Permute (Typed version)
// Expected result: 8660

type PState = {count: int}

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn swap(v: int[], i: int, j: int) {
    var tmp = v[i]
    v[i] = v[j]
    v[j] = tmp
}

pn permute(state: PState, v: int[], n: int) {
    state.count = state.count + 1
    if (n != 0) {
        var n1: int = n - 1
        permute(state, v, n1)
        var i: int = n1
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
    var v:int[] = make_array(6, 0)
    permute(state, v, 6)
    return state.count
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
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
