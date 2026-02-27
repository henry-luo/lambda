// BENG Benchmark: binary-trees
// Allocate/deallocate binary trees of various depths to stress-test GC
// N=10 expected: matches benchmarksgame binarytrees-output.txt

let N = 10

pn make_tree(depth) {
    if (depth == 0) {
        return {left: null, right: null}
    }
    return {left: make_tree(depth - 1), right: make_tree(depth - 1)}
}

pn check(node) {
    if (node.left == null) {
        return 1
    }
    return 1 + check(node.left) + check(node.right)
}

pn main() {
    let min_depth = 4
    var max_depth = N
    if (min_depth + 2 > max_depth) {
        max_depth = min_depth + 2
    }
    let stretch_depth = max_depth + 1

    // stretch tree
    let stretch = make_tree(stretch_depth)
    print("stretch tree of depth " ++ string(stretch_depth) ++ "\t check: " ++ string(check(stretch)) ++ "\n")

    // long-lived tree
    let long_lived = make_tree(max_depth)

    var depth = min_depth
    while (depth <= max_depth) {
        let iterations = shl(1, max_depth - depth + min_depth)
        var total_check = 0
        var i = 0
        while (i < iterations) {
            total_check = total_check + check(make_tree(depth))
            i = i + 1
        }
        print(string(iterations) ++ "\t trees of depth " ++ string(depth) ++ "\t check: " ++ string(total_check) ++ "\n")
        depth = depth + 2
    }

    print("long lived tree of depth " ++ string(max_depth) ++ "\t check: " ++ string(check(long_lived)) ++ "\n")
}
