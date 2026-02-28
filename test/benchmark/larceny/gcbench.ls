// Larceny Benchmark: gcbench
// GC stress test — allocate and check binary trees of increasing depth
// Adapted from Larceny/Gambit gcbench (Hans Boehm's GC benchmark)
// Creates trees of depth 4..14 (stretch to 15), checks node counts

pn make_tree(depth) {
    if (depth == 0) {
        return {left: null, right: null}
    }
    return {left: make_tree(depth - 1), right: make_tree(depth - 1)}
}

pn check_tree(node) {
    if (node.left == null) {
        return 1
    }
    return 1 + check_tree(node.left) + check_tree(node.right)
}

pn populate(depth, node) {
    if (depth <= 0) {
        return 0
    }
    node.left = {left: null, right: null}
    node.right = {left: null, right: null}
    populate(depth - 1, node.left)
    populate(depth - 1, node.right)
    return 0
}

pn main() {
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

    print("long lived tree of depth " ++ string(max_depth) ++ " check: " ++ string(check_tree(long_lived)) ++ "\n")
}
