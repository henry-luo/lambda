// Compact-integer payloads are immediate Items, so this fresh recursive graph
// can use a call-scoped region while the consumer returns only its aggregate.

pn make_weighted_tree_region(depth: int) {
    if (depth == 0) {
        return {value: 1, left: null, right: null}
    }
    return {
        value: depth,
        left: make_weighted_tree_region(depth - 1),
        right: make_weighted_tree_region(depth - 1)
    }
}

pn sum_weighted_tree_region(node) {
    if (node.left == null) {
        return node.value
    }
    return node.value + sum_weighted_tree_region(node.left) +
        sum_weighted_tree_region(node.right)
}

pn main() {
    let persistent = make_weighted_tree_region(3)
    print(sum_weighted_tree_region(make_weighted_tree_region(4)) ++ ":" ++
        sum_weighted_tree_region(persistent))
}
