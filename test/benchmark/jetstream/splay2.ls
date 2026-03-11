// JetStream Benchmark: splay (Octane)
// Splay tree — self-balancing BST with frequent insert/delete
// Original: V8 project authors
// Measures allocation, GC pressure, and tree manipulation

let TREE_SIZE = 8000
let TREE_MODIFICATIONS = 80

// Type definitions for direct struct field access
// Field order must match the map literal order in create_node
type SplayNode = {key: float, left: SplayNode, right: SplayNode, value: map}
type SplayTree = {root: SplayNode}
type RngState = {seed: float}
type PayloadLeaf = {arr: array, str: float}
type PayloadBranch = {left_p: map, right_p: map}

// Node: {key, left, right, value}
// SplayNode type annotation ensures runtime data layout matches direct access offsets
pn create_node(key: float, value) {
    var node: SplayNode = {key: key, left: null, right: null, value: value}
    return node
}

// Simple LCG pseudo-random number generator (deterministic)
// Use float arithmetic to avoid integer overflow
pn next_random(state: RngState) {
    var s = state.seed
    // Split multiplication to avoid overflow: 1103515245 = 1103515 * 1000 + 245
    // Use modular arithmetic in parts
    var hi = s / 127773           // q = 2147483648 / 16807 ≈ 127773
    var lo = s % 127773
    s = 16807 * lo - 2836 * hi
    if (s <= 0) {
        s = s + 2147483647
    }
    state.seed = s
    return float(s) / 2147483647.0
}

// Splay tree using maps for tree state
pn splay_tree_new() {
    var tree: SplayTree = {root: null}
    return tree
}

pn splay_is_empty(tree: SplayTree) {
    return tree.root == null
}

pn splay(tree: SplayTree, key: float) {
    if (splay_is_empty(tree)) {
        return 0
    }
    var dummy: SplayNode = create_node(0.0, null)
    var left: SplayNode = dummy
    var right: SplayNode = dummy
    var current: SplayNode = tree.root
    var done = false
    while (done == false) {
        if (key < current.key) {
            if (current.left == null) {
                done = true
            } else {
                if (key < (current.left).key) {
                    // rotate right
                    var tmp: SplayNode = current.left
                    current.left = tmp.right
                    tmp.right = current
                    current = tmp
                    if (current.left == null) {
                        done = true
                    }
                }
                if (done == false) {
                    // link right
                    right.left = current
                    right = current
                    current = current.left
                }
            }
        } else {
            if (key > current.key) {
                if (current.right == null) {
                    done = true
                } else {
                    if (key > (current.right).key) {
                        // rotate left
                        var tmp: SplayNode = current.right
                        current.right = tmp.left
                        tmp.left = current
                        current = tmp
                        if (current.right == null) {
                            done = true
                        }
                    }
                    if (done == false) {
                        // link left
                        left.right = current
                        left = current
                        current = current.right
                    }
                }
            } else {
                done = true
            }
        }
    }
    // assemble
    left.right = current.left
    right.left = current.right
    current.left = dummy.right
    current.right = dummy.left
    tree.root = current
    return 0
}

pn splay_insert(tree: SplayTree, key: float, value) {
    if (splay_is_empty(tree)) {
        tree.root = create_node(key, value)
        return 0
    }
    splay(tree, key)
    if ((tree.root).key == key) {
        return 0
    }
    var node: SplayNode = create_node(key, value)
    if (key > (tree.root).key) {
        node.left = tree.root
        node.right = (tree.root).right
        var root_ref: SplayNode = tree.root
        root_ref.right = null
    } else {
        node.right = tree.root
        node.left = (tree.root).left
        var root_ref: SplayNode = tree.root
        root_ref.left = null
    }
    tree.root = node
    return 0
}

pn splay_remove(tree: SplayTree, key: float) {
    if (splay_is_empty(tree)) {
        return null
    }
    splay(tree, key)
    if ((tree.root).key != key) {
        return null
    }
    var removed: SplayNode = tree.root
    if ((tree.root).left == null) {
        tree.root = (tree.root).right
    } else {
        var right_tree: SplayNode = (tree.root).right
        tree.root = (tree.root).left
        splay(tree, key)
        var root_ref: SplayNode = tree.root
        root_ref.right = right_tree
    }
    return removed
}

pn splay_find(tree: SplayTree, key: float) {
    if (splay_is_empty(tree)) {
        return null
    }
    splay(tree, key)
    if ((tree.root).key == key) {
        return tree.root
    }
    return null
}

pn splay_find_max(node: SplayNode) {
    while (node.right != null) {
        node = node.right
    }
    return node
}

pn splay_find_greatest_less_than(tree: SplayTree, key: float) {
    if (splay_is_empty(tree)) {
        return null
    }
    splay(tree, key)
    if ((tree.root).key < key) {
        return tree.root
    }
    if ((tree.root).left != null) {
        return splay_find_max((tree.root).left)
    }
    return null
}

// Count nodes for verification
pn count_nodes(node: SplayNode) {
    if (node == null) {
        return 0
    }
    return 1 + count_nodes(node.left) + count_nodes(node.right)
}

// Collect keys in-order for verification
pn traverse_keys(node: SplayNode, keys, idx_in) {
    if (node == null) {
        return idx_in
    }
    var idx = traverse_keys(node.left, keys, idx_in)
    keys[idx] = node.key
    idx = idx + 1
    idx = traverse_keys(node.right, keys, idx)
    return idx
}

// Generate payload tree for node values
pn generate_payload(depth: int, tag: float) {
    if (depth == 0) {
        var leaf: PayloadLeaf = {arr: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], str: tag}
        return leaf
    }
    var branch: PayloadBranch = {left_p: generate_payload(depth - 1, tag),
            right_p: generate_payload(depth - 1, tag)}
    return branch
}

pn insert_new_node(tree: SplayTree, rng: RngState) {
    var key = next_random(rng)
    while (splay_find(tree, key) != null) {
        key = next_random(rng)
    }
    var payload = generate_payload(5, key)
    splay_insert(tree, key, payload)
    return key
}

pn run_splay() {
    var tree: SplayTree = splay_tree_new()
    var rng: RngState = {seed: 49734321}

    // Setup: insert TREE_SIZE nodes
    var i: int = 0
    while (i < TREE_SIZE) {
        insert_new_node(tree, rng)
        i = i + 1
    }

    // Run: do TREE_MODIFICATIONS insert/delete cycles (like JetStream's SplayRun x50)
    var iter: int = 0
    while (iter < 50) {
        var j: int = 0
        while (j < TREE_MODIFICATIONS) {
            var key = insert_new_node(tree, rng)
            var greatest: SplayNode = splay_find_greatest_less_than(tree, key)
            if (greatest == null) {
                splay_remove(tree, key)
            } else {
                splay_remove(tree, greatest.key)
            }
            j = j + 1
        }
        iter = iter + 1
    }

    // Verify: should still have TREE_SIZE nodes
    var node_count = count_nodes(tree.root)
    return node_count
}

pn main() {
    var __t0 = clock()
    let count = run_splay()
    var __t1 = clock()
    if (count == TREE_SIZE) {
        print("splay: PASS (nodes=" ++ string(count) ++ ")\n")
    } else {
        print("splay: FAIL (nodes=" ++ string(count) ++ ", expected " ++ string(TREE_SIZE) ++ ")\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
