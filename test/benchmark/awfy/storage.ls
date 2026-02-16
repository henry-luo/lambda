// AWFY Benchmark: Storage
// Expected result: 5461
// Recursive tree of arrays (allocation stress test)

pn random_next(seed_arr) {
    var s = seed_arr[0]
    s = s * 1309 + 13849
    s = s % 65536
    seed_arr[0] = s
    return s
}

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

pn build_tree_depth(state, depth, seed_arr) {
    state.count = state.count + 1
    if (depth == 1) {
        return make_array((random_next(seed_arr) % 10) + 1, 0)
    }
    var arr = [null, null, null, null]
    var i = 0
    while (i < 4) {
        arr[i] = build_tree_depth(state, depth - 1, seed_arr)
        i = i + 1
    }
    return arr
}

pn benchmark() {
    var seed_arr = [74755]
    let state = {count: 0}
    build_tree_depth(state, 7, seed_arr)
    return state.count
}

pn main() {
    let result = benchmark()
    if (result == 5461) {
        print("Storage: PASS\n")
    } else {
        print("Storage: FAIL result=")
        print(result)
        print("\n")
    }
}
