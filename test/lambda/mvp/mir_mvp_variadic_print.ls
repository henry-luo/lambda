// S7.9.3: print inspects values; argument effects retain source order.
pn observed(value: int) int {
    print("<"); print(value); print(">")
    return value
}
pn main() {
    print("A", observed(2), "C"); print("\n")
    print("["); print(); print("]\n")
    let result = print("value", 3, true, [1, 2])
    print("\n"); print(result is null); print("\n")
    print(1e-310, observed(4), 2e-310); print("\n")
}
