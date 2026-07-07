// Phase 5: mutable bindings receive recursive container copies.

pn main() {
    let g = [[1, 2], [3, 4]]
    var h = g
    h[0][0] = 99
    print((g[0][0]) ++ " " ++ (h[0][0]) ++ "\n")
}
