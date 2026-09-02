// JetStream Benchmark: splay (Octane)
// Splay tree — self-balancing BST with frequent insert/delete
// Original: V8 project authors
// Measures allocation, GC pressure, and tree manipulation

let TREE_SIZE = 8000
let TREE_MODIFICATIONS = 80

// Type definitions for direct struct field access
// Field order must match the map literal order in create_node
type SplayNode = {key: float, left: SplayNode?, right: SplayNode?, value: map?}
// The root carries the same fixed record contract as nodes; keeping it open
// forces every root handoff through runtime map admission and erases C3's
// direct-shape proof.
type SplayTree = {root: SplayNode?}
type RngState = {seed: int}
type PayloadLeaf = {arr: array, str: float}
type PayloadBranch = {left_p: map, right_p: map}

// Node: {key, left, right, value}
// SplayNode type annotation ensures runtime data layout matches direct access offsets
pn create_node(key: float, value) SplayNode {
    var node: SplayNode = {key: key, left: null, right: null, value: value}
    return node
}

// Simple LCG pseudo-random number generator (deterministic)
// u32 arithmetic intentionally wraps like the original 32-bit PRNG.
// The PRNG and tree are explicit inout values; ordinary parameters are
// snapshots under COW and would lose the state update that makes keys unique.
pn next_random(var st: RngState) float {
    var s: u32 = st.seed
    s = s * 1103515245u32 + 12345u32
    st.seed = int(s)
    return float(s) / 4294967296.0
}

// Splay tree using maps for tree state
pn splay_tree_new() SplayTree {
    var tree: SplayTree = {root: null}
    return tree
}

pn splay_is_empty(tree: SplayTree) bool {
    return tree.root == null
}

pn splay(var tree: SplayTree, key: float) int {
    if (splay_is_empty(tree)) {
        return 0
    }
    // Rebuild each rotated subtree through a local owner before returning it.
    // The original top-down cursor links depended on mutable pointer aliases,
    // which are snapshots under COW (D3.3.1).
    var root: SplayNode? = tree.root
    root = splay_node(root, key)
    tree.root = root
    return 0
}

pn rotate_right(var node: SplayNode?) SplayNode? {
    if (node == null) {
        return node
    }
    if (node.left == null) {
        return node
    }
    var left: SplayNode? = node.left
    var branch = left.right
    node.left = branch
    left.right = node
    return left
}

pn rotate_left(var node: SplayNode?) SplayNode? {
    if (node == null) {
        return node
    }
    if (node.right == null) {
        return node
    }
    var right: SplayNode? = node.right
    var branch = right.left
    node.right = branch
    right.left = node
    return right
}

pn splay_node(var node: SplayNode?, key: float) SplayNode? {
    if (node == null) {
        return null
    }
    if (key < node.key) {
        if (node.left == null) {
            return node
        }
        var left: SplayNode? = node.left
        if (key < left.key) {
            var branch: SplayNode? = left.left
            branch = splay_node(branch, key)
            left.left = branch
            node.left = left
            node = rotate_right(node)
        }
        if (key > left.key) {
            var branch: SplayNode? = left.right
            branch = splay_node(branch, key)
            left.right = branch
            if (left.right != null) {
                left = rotate_left(left)
            }
            node.left = left
        }
        if (node.left == null) {
            return node
        }
        return rotate_right(node)
    }
    if (key > node.key) {
        if (node.right == null) {
            return node
        }
        var right: SplayNode? = node.right
        if (key > right.key) {
            var branch: SplayNode? = right.right
            branch = splay_node(branch, key)
            right.right = branch
            node.right = right
            node = rotate_left(node)
        }
        if (key < right.key) {
            var branch: SplayNode? = right.left
            branch = splay_node(branch, key)
            right.left = branch
            if (right.left != null) {
                right = rotate_right(right)
            }
            node.right = right
        }
        if (node.right == null) {
            return node
        }
        return rotate_left(node)
    }
    return node
}

pn splay_insert(var tree: SplayTree, key: float, value) int {
    if (splay_is_empty(tree)) {
        tree.root = create_node(key, value)
        return 0
    }
    splay(tree, key)
    if ((tree.root).key == key) {
        return 0
    }
    var node = create_node(key, value)
    var old_root: SplayNode? = tree.root
    if (key > (tree.root).key) {
        node.left = old_root
        node.right = old_root.right
        old_root.right = null
        node.left = old_root
    } else {
        node.right = old_root
        node.left = old_root.left
        old_root.left = null
        node.right = old_root
    }
    tree.root = node
    return 0
}

pn splay_remove(var tree: SplayTree, key: float) map? {
    if (splay_is_empty(tree)) {
        return null
    }
    splay(tree, key)
    if ((tree.root).key != key) {
        return null
    }
    var removed = tree.root
    if (removed.left == null) {
        tree.root = removed.right
    } else {
        var left_tree: SplayNode? = removed.left
        var right_tree: SplayNode? = removed.right
        left_tree = splay_node(left_tree, key)
        left_tree.right = right_tree
        tree.root = left_tree
    }
    return removed
}

pn splay_find(var tree: SplayTree, key: float) map? {
    if (splay_is_empty(tree)) {
        return null
    }
    splay(tree, key)
    if ((tree.root).key == key) {
        return tree.root
    }
    return null
}

pn splay_find_max(node: SplayNode) SplayNode {
    while (node.right != null) {
        node = node.right
    }
    return node
}

pn splay_find_greatest_less_than(var tree: SplayTree, key: float) map? {
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
pn count_nodes(node: SplayNode?) int {
    if (node == null) {
        return 0
    }
    return 1 + count_nodes(node.left) + count_nodes(node.right)
}

// Collect keys in-order for verification
pn traverse_keys(node: SplayNode?, var keys: float[], idx_in: int) int {
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
pn generate_payload(depth: int, tag: float) map {
    if (depth == 0) {
        var leaf = {arr: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], str: tag}
        return leaf
    }
    var branch = {left_p: generate_payload(depth - 1, tag),
            right_p: generate_payload(depth - 1, tag)}
    return branch
}

pn insert_new_node(var tree: SplayTree, var rng: RngState) float {
    var key = next_random(rng)
    while (splay_find(tree, key) != null) {
        key = next_random(rng)
    }
    var payload = generate_payload(5, key)
    splay_insert(tree, key, payload)
    return key
}

pn run_splay() int {
    var tree = splay_tree_new()
    var rng = {seed: 49734321}

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
            var greatest = splay_find_greatest_less_than(tree, key)
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
        print("splay: PASS (nodes=" ++ count ++ ")\n")
    } else {
        print("splay: FAIL (nodes=" ++ count ++ ", expected " ++ TREE_SIZE ++ ")\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
