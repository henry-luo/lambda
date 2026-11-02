// S4.1.2, S9.1.2: native modular updates retain poison, bounds and snapshots.
pn step(var a: int[], k: int) any { a[k] = (a[k] + 3) % 16 }
pn down(var a: int[]) any { a[0] = (a[0] - 3) % 16 }
pn dynamic(seed: any) any {
    var a = fill(1, seed)
    a[0] = (a[0] + 3) % 16
    return a[0]
}
pn remainder(a: int) int { return a % 16 }
pn quotient(a: int) int { return a div 16 }
pn large(a: int) int { return a % 4503599627370496 }
pn print_divmod(a: int) {
    print(remainder(a)); print(" "); print(quotient(a)); print(" "); print(large(a)); print("\n")
}
pn main() {
    var a: int[] = [14, -8, 9007199254740991, int(inf)]
    let saved: int[] = a^
    step(a, 0)^; step(a, 1)^; step(a, 2)^; step(a, 3)^
    print(a); print("\n"); print(saved); print("\n")
    down(a)^; print(a[0]); print("\n")
    step(a, -1)^; step(a, 4)^
    print(a); print("\n")
    print(dynamic(14)); print(" "); print(dynamic(-8)); print(" "); print(dynamic(1.5)); print("\n")
    print(dynamic("bad")); print("\n")
    print_divmod(33); print_divmod(-33); print_divmod(-32); print_divmod(0)
    print_divmod(9007199254740991); print_divmod(-9007199254740991)
    print_divmod(int(inf))^; print_divmod(int(-inf))^; print_divmod(int(nan))
}
