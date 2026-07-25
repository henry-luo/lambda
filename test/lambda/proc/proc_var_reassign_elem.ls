// Reassigning a var rebinds the name to a different container, so no
// element-type narrowing taken at the declaration survives the assignment.
// Regression: reads after a reassignment returned <error> because the
// narrowing kept promising a native element while the index lowering had
// switched to the boxed path.

// levenshtein-style row swap: the read is lowered before the reassignment
// but executes after it on every later iteration.
pn swap_rows(n) {
    var prev = fill(n, 1)
    var curr = fill(n, 2)
    var i = 0
    var total = 0
    while (i < n) {
        total = total + prev[0] + curr[0]
        prev = curr
        curr = fill(n, 3)
        i = i + 1
    }
    return total
}

pn main() {
    // var-to-var reassignment, element type unchanged
    var a = fill(4, 7)
    var b = fill(4, 3)
    a = b
    print("reassign_same_elem=" ++ string(a[0]) ++ "\n")

    // var-to-var reassignment that changes the element representation
    var c = fill(4, 7)
    var d = fill(4, 1.5)
    c = d
    print("reassign_float=" ++ string(c[0]) ++ "\n")

    // reassignment straight from a call
    var e = fill(4, 7)
    e = fill(4, 2.5)
    print("reassign_call=" ++ string(e[0]) ++ "\n")

    // literal arrays
    var g = [1, 2, 3]
    var h = [4, 5, 6]
    g = h
    print("reassign_literal=" ++ string(g[0]) ++ "\n")

    // reads on both sides of the reassignment
    var p = fill(3, 9)
    var q = fill(3, 4)
    print("before=" ++ string(p[0]) ++ "\n")
    p = q
    print("after=" ++ string(p[0]) ++ "\n")

    // the reassigned source keeps its own value
    print("source_intact=" ++ string(q[0]) ++ "\n")

    print("swap=" ++ string(swap_rows(3)) ++ "\n")
}
