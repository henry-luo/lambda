fn recurse(n) => n + recurse(n + 1)
pn main() { let unused = recurse(0); print("bound: after overflow") }
