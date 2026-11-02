// S12.1.2v3/D2.2.2: statement-bodied fn kernels retain the shared MIR proofs.
fn make_tree(depth: int) {
    if (depth == 0) { return {left: null, right: null} }
    return {left: make_tree(depth - 1), right: make_tree(depth - 1)}
}
fn count_nodes(node) {
    if (node.left == null) { return 1 }
    return 1 + count_nodes(node.left) + count_nodes(node.right)
}
fn choose(key: int) {
    var result = 0
    if (key == 11) { result = result + 2 }
    if (key == 22) { result = result + 3 }
    if (key == 33) { result = result + 5 }
    return result
}
fn forward(text) { return text }
fn decorate(text) { return "<" ++ forward(text) ++ ">" };
[count_nodes(make_tree(5)) == 63, choose(22) == 3, choose(0) == 0,
 decorate("ok") == "<ok>"]
