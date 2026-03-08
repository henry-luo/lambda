// Test MIR JIT parameter type inference for procedural functions.
// Bug 4: When an untyped parameter is used only in comparisons (==, <, >)
// with non-literal operands, it should NOT be inferred as INT.
// Comparisons are polymorphic — the parameter may hold float values.

// Test 1: Untyped param used in == comparison with map field
// The param `key` must remain ANY (not wrongly inferred as INT)
pn test_cmp_with_field(tree, key) {
    if (tree.root != null) {
        if ((tree.root).key == key) {
            return "equal"
        }
    }
    return "not_equal"
}

// Test 2: Untyped param used in < comparison with map field
pn test_lt_with_field(tree, key) {
    if (tree.root != null) {
        if (key < (tree.root).key) {
            return "less"
        }
    }
    return "not_less"
}

// Test 3: Untyped param used in > comparison with map field
pn test_gt_with_field(tree, key) {
    if (tree.root != null) {
        if (key > (tree.root).key) {
            return "greater"
        }
    }
    return "not_greater"
}

// Test 4: Untyped param used in arithmetic — INT inference is correct here
pn test_arith_int(n) {
    return n + 1
}

// Test 5: Untyped param used in arithmetic with float literal — FLOAT inference
pn test_arith_float(n) {
    return n + 1.0
}

// Test 6: Chain of float comparisons (simulates splay tree key routing)
pn insert_key(tree, key, value) {
    if (tree.root == null) {
        tree.root = {key: key, left: null, right: null, value: value}
        return 0
    }
    if ((tree.root).key == key) {
        // update value
        (tree.root).value = value
        return 0
    }
    if (key < (tree.root).key) {
        // insert left
        (tree.root).left = {key: key, left: null, right: null, value: value}
        return 0
    }
    // insert right
    (tree.root).right = {key: key, left: null, right: null, value: value}
    return 0
}

pn main() {
    // Test 1: float key compared with map field via ==
    var tree1 = {root: {key: 0.5, left: null, right: null, value: "a"}}
    var r1 = test_cmp_with_field(tree1, 0.5)
    var r1b = test_cmp_with_field(tree1, 0.7)
    print("T1:" ++ r1 ++ "," ++ r1b)

    // Test 2: float key compared with map field via <
    var tree2 = {root: {key: 0.5, left: null, right: null, value: "b"}}
    var r2 = test_lt_with_field(tree2, 0.3)
    var r2b = test_lt_with_field(tree2, 0.7)
    print("T2:" ++ r2 ++ "," ++ r2b)

    // Test 3: float key compared with map field via >
    var tree3 = {root: {key: 0.5, left: null, right: null, value: "c"}}
    var r3 = test_gt_with_field(tree3, 0.7)
    var r3b = test_gt_with_field(tree3, 0.3)
    print("T3:" ++ r3 ++ "," ++ r3b)

    // Test 4: int arithmetic still works (INT inference for arithmetic is correct)
    var r4 = test_arith_int(10)
    print("T4:" ++ string(r4))

    // Test 5: float arithmetic with literal
    var r5 = test_arith_float(2.5)
    print("T5:" ++ string(r5))

    // Test 6: chain insert with float keys
    var tree6 = {root: null}
    insert_key(tree6, 0.5, "first")
    insert_key(tree6, 0.5, "updated")
    insert_key(tree6, 0.3, "left")
    insert_key(tree6, 0.7, "right")
    print("T6:" ++ (tree6.root).value ++ "," ++ ((tree6.root).left).key ++ "," ++ ((tree6.root).right).key)
}
