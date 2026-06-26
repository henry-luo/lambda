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

pn build_tree_depth(state, depth, seed_arr) {
    state.count = state.count + 1
    if (depth == 1) {
        return fill((random_next(seed_arr) % 10) + 1, 0)
    }
    var arr = fill(4, null)
    for i in 0 to 3 {
        arr[i] = build_tree_depth(state, depth - 1, seed_arr)
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
