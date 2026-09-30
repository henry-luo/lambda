// D2.2.2/S4.1.2: the len() upper bound makes these neighboring sums differ
// by one at INT53_MAX, even when long double has only binary64 precision.
pn int_interval_safe(values: int[]) int {
    return len(values) * 4194304 + 4194303
}

pn int_interval_crosses(values: int[]) int {
    return len(values) * 4194304 + 4194304
}

pn main() {
    let values: int[] = [1, 2]
    print(int_interval_safe(values)); print(" ")
    print(int_interval_crosses(values)); print("\n")
}
