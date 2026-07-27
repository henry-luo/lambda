pn make_tree_region(depth) {
    if (depth == 0) {
        return {left: null, right: null}
    }
    return {left: make_tree_region(depth - 1), right: make_tree_region(depth - 1)}
}

pn check_tree_region(node) {
    if (node.left == null) {
        return 1
    }
    return 1 + check_tree_region(node.left) + check_tree_region(node.right)
}

pn main() {
    let persistent = make_tree_region(3)
    print(check_tree_region(make_tree_region(4)) ++ ":" ++ check_tree_region(persistent))
}
