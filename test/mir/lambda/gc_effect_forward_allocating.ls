// D5.3.2: a later callee that allocates keeps the caller's live string rooted.
pn gc_order_alloc_caller(s: string, x: int) int {
    return gc_order_alloc_leaf(x) + len(s)
}

pn gc_order_alloc_leaf(x: int) int {
    let values = fill(2, x)
    return len(values)
}

pn main() { print(gc_order_alloc_caller("abcd", 2)); print("\n") }
