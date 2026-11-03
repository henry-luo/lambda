// S9.1.2–S9.1.3: admitted constructors still preserve snapshots and error boundaries.
pn sum(a: int[]) int { return a[0] + a[1] }
pn freshly(count: int, seed: int) int^ {
    var a: int[] = fill(count, seed)^
    a
    let saved: int[] = a
    a[0] = 8
    return a[0] + saved[0]
}
pn call_fill(count: int, seed: any) int^ { return sum(fill(count, seed)) }
pn mark(seed: int) int { print(seed); print(" "); return seed }
pn choose(a: int[], b: int[]) int { return a[0] + b[0] }
pn float_values(seed: float) any { var a: float[] = fill(2, seed)^; return a }
pn bool_values(seed: bool) any { var a: bool[] = fill(3, seed)^; return a }
pn int_values(count: int, seed: int) any { var a: int[] = fill(count, seed)^; return a }
pn counted_values(count: int) any { var a: int[2] = fill(count, 3)^; return a }
pn constant_argument() int^ { return sum(fill(2, 7)) }
pn collect_and_sum(a: int[]) int^ { let waste: int[] = fill(32, 9)^; return a[0] + a[1] }
pn mutate_plain(a: int[]) int { a[0] = 9; return a[0] }
pn mutable_argument() int^ { return mutate_plain(fill(2, 7)) }
pn retain(a: int[]) any { return a }
pn retained_argument() any { return retain(fill(2, 7)) }
pn readonly_length(a: int[]) int { return len(a) }
pn forward_read(a: int[]) int^ { return sum(a) }
pn forwarded_argument() int^ { return forward_read(fill(2, 7)) }
pn main() {
    print(freshly(2, 3)); print("\n")
    print(call_fill(2, 7)); print("\n")
    print(choose(fill(2, mark(1)), fill(2, mark(2)))); print("\n")
    let failed_count = call_fill(-1, 1)
    if (not (failed_count is error)) { print("UNEXPECTED COUNT\n") }
    let failed_element = call_fill(2, "bad")
    if (not (failed_element is error)) { print("UNEXPECTED ELEMENT\n") }
    let failed_poison = freshly(int(inf), 1)
    if (not (failed_poison is error)) { print("UNEXPECTED POISON\n") }
    print("after rejected constructors\n")
    print(float_values(-0.0)); print(" "); print(float_values(nan)); print("\n")
    print(bool_values(true)); print(" "); print(bool_values(false)); print("\n")
    print(int_values(0, 3)); print(" "); print(int_values(2, -7)); print("\n")
    print(int_values(2, int(inf))); print(" "); print(int_values(2, int(nan))); print("\n")
    print(counted_values(2)); print(" "); print(counted_values(1)); print("\n")
    print(constant_argument()); print(" "); print(collect_and_sum(fill(2, 7))); print("\n")
    print(mutable_argument()); print(" "); print(constant_argument()); print("\n")
    var escaped: int[] = retained_argument()
    escaped[0] = 8
    print(escaped); print("\n")
    print(readonly_length(fill(0, 7))); print(" "); print(readonly_length(fill(65, 7))); print("\n")
    print(forwarded_argument()); print("\n")
}
