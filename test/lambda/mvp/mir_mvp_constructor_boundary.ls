// S9.1.2–S9.1.3: admitted constructors still preserve snapshots and error boundaries.
pn sum(a: int[]) int { return a[0] + a[1] }
pn freshly(count: int, seed: int) int {
    var a: int[] = fill(count, seed)
    let saved: int[] = a
    a[0] = 8
    return a[0] + saved[0]
}
pn call_fill(count: int, seed: any) int { return sum(fill(count, seed)) }
pn mark(seed: int) int { print(seed); print(" "); return seed }
pn choose(a: int[], b: int[]) int { return a[0] + b[0] }
pn main() {
    print(freshly(2, 3)); print("\n")
    print(call_fill(2, 7)); print("\n")
    print(choose(fill(2, mark(1)), fill(2, mark(2)))); print("\n")
    if (true) { let failed: int = call_fill(-1, 1); print("UNEXPECTED COUNT\n") }
    if (true) { let failed: int = call_fill(2, "bad"); print("UNEXPECTED ELEMENT\n") }
    if (true) { let failed: int = freshly(int(inf), 1); print("UNEXPECTED POISON\n") }
    print("after rejected constructors\n")
}
