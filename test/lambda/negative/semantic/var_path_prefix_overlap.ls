// Negative test: exclusivity face 3 (S9.1.3, COW §11.3) -- a `var` argument
// whose path is a PREFIX of another `var` argument's path names overlapping
// storage; two writers over one region are rejected at whole-base
// granularity (E211).
// Expected error: E211 "overlaps another `var` parameter"

pn two(var whole, var part) {
    whole.count = 0
    part[0] = 9
}

pn main() {
    var t = {count: 1, nodes: [1, 2, 3]}
    two(t, t.nodes)
    print(t.count)
}
