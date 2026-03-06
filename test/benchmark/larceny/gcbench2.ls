// Larceny Benchmark: gcbench2 (typed maps variant)
// GC stress test with typed maps — validates GC correctness under allocation pressure
// Uses typed Node maps with container fields to stress GC compaction + tracing

type Node = {left: map, right: map}

pn make_tree(depth: int) {
    if (depth == 0) {
        var n: Node = {left: null, right: null}
        return n
    }
    var n: Node = {left: make_tree(depth - 1), right: make_tree(depth - 1)}
    return n
}

pn check_tree(node: Node) {
    if (node.left == null) {
        return 1
    }
    return 1 + check_tree(node.left) + check_tree(node.right)
}

pn main() {
    var __t0 = clock()
    let min_depth = 4
    let max_depth = 14
    let stretch_depth = max_depth + 1

    // Stretch tree
    let stretch = make_tree(stretch_depth)
    print("stretch tree of depth " ++ string(stretch_depth) ++ " check: " ++ string(check_tree(stretch)) ++ "\n")

    // Long-lived tree
    let long_lived = make_tree(max_depth)

    var depth = min_depth
    while (depth <= max_depth) {
        let iterations = shl(1, max_depth - depth + min_depth)
        var total_check = 0
        var i = 0
        while (i < iterations) {
            total_check = total_check + check_tree(make_tree(depth))
            i = i + 1
        }
        print(string(iterations) ++ " trees of depth " ++ string(depth) ++ " check: " ++ string(total_check) ++ "\n")
        depth = depth + 2
    }
    var __t1 = clock()

    print("long lived tree of depth " ++ string(max_depth) ++ " check: " ++ string(check_tree(long_lived)) ++ "\n")
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
