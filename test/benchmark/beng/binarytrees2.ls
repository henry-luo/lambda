// BENG Benchmark: binary-trees (Typed version)
// Allocate/deallocate binary trees of various depths to stress-test GC
// N=10 expected: matches benchmarksgame binarytrees-output.txt

let N = 10

// named node shape, used on the check() parameter to document the tree shape
type Node = {left: Node?, right: Node?}

// fresh trees use the shared GC map allocator; the checker retains typed field access.
fn make_tree(depth: int) Node {
    if (depth == 0) {
        return {left: null, right: null}
    }
    return {left: make_tree(depth - 1), right: make_tree(depth - 1)}
}

fn check(node: Node) int^ {
    if (node.left == null) {
        return 1
    }
    return 1 + check(node.left) + check(node.right)
}

pn main() {
    var __t0 = clock()
    let min_depth: int = 4
    var max_depth: int = N
    if (min_depth + 2 > max_depth) {
        max_depth = min_depth + 2
    }
    let stretch_depth: int = max_depth + 1

    // stretch tree
    let stretch = make_tree(stretch_depth)
    print("stretch tree of depth " ++ stretch_depth ++ "\t check: " ++ check(stretch) ++ "\n")

    // long-lived tree
    let long_lived = make_tree(max_depth)

    var depth: int = min_depth
    while (depth <= max_depth) {
        let iterations: int = shl(1, max_depth - depth + min_depth)
        var total_check: int = 0
        var i: int = 0
        while (i < iterations) {
            total_check = total_check + check(make_tree(depth))
            i = i + 1
        }
        print(iterations ++ "\t trees of depth " ++ depth ++ "\t check: " ++ total_check ++ "\n")
        depth = depth + 2
    }

    print("long lived tree of depth " ++ max_depth ++ "\t check: " ++ check(long_lived) ++ "\n")
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
