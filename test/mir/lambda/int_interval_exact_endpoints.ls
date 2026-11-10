// D2.2.2/S4.1.2: explicit bounds make these sums differ by one at
// INT53_MAX. Array lengths are int64, so they cannot imply the old int32 bound.
pn int_interval_safe(count: int) int {
    if (count >= 0 and count <= 2147483647) {
        return count * 4194304 + 4194303
    }
    return 0
}

pn int_interval_crosses(count: int) int {
    if (count >= 0 and count <= 2147483647) {
        return count * 4194304 + 4194304
    }
    return 0
}

pn main() {
    print(int_interval_safe(2)); print(" ")
    print(int_interval_crosses(2)); print(" ")
    print(int_interval_safe(2147483647)); print(" ")
    print(int_interval_crosses(2147483647)); print(" ")
    print(int_interval_safe(9007199254740991)); print(" ")
    print(int_interval_crosses(-1)); print("\n")
}
