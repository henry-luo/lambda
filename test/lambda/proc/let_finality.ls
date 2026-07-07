// Phase 5: var aliases detach from let roots before interior mutation.

pn main() {
    let g = [4, 5, 6]
    var h = g
    h[0] = 77
    print((g[0]) ++ " " ++ (h[0]) ++ "\n")
}
