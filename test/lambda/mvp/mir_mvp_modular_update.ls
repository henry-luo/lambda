// S4.1.2, S9.1.2: native modular updates retain poison, bounds and snapshots.
pn step(var a: int[], k: int) any { a[k] = (a[k] + 3) % 16 }
pn down(var a: int[]) any { a[0] = (a[0] - 3) % 16 }
let MODULUS = 31
let INCREMENT = 7
pn affine(var a: int[], k: int) any { a[k] = int((a[k] * 13 + INCREMENT) % MODULUS)^ }
pn affine_cancel(var a: int[]) any { a[0] = int((a[0] * 2 - 9007199254740991) % MODULUS)^ }
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
    let saved: int[] = a
    step(a, 0)^; step(a, 1)^; step(a, 2)^; step(a, 3)^
    print(a); print("\n"); print(saved); print("\n")
    down(a)^; print(a[0]); print("\n")
    let negative_index = step(a, -1)
    let missing_index = step(a, 4)
    print([negative_index is error, missing_index is error]); print("\n")
    print(a); print("\n")
    print(dynamic(14)); print(" "); print(dynamic(-8)); print(" "); print(dynamic(1.5)); print("\n")
    print(dynamic("bad")); print("\n")
    print_divmod(33); print_divmod(-33); print_divmod(-32); print_divmod(0)
    print_divmod(9007199254740991); print_divmod(-9007199254740991)
    print_divmod(int(inf)); print_divmod(int(-inf)); print_divmod(int(nan))
    var seeds: int[] = [2, -2, 9007199254740991, int(inf)]
    let old_seeds = seeds
    affine(seeds, 0)^; affine(seeds, 1)^; affine(seeds, 2)^; affine(seeds, 3)^
    print(seeds); print("\n"); print(old_seeds); print("\n")
    let invalid = affine(seeds, -1)
    print(match invalid { case error: true default: false }); print("\n")
    var over: int[] = [9007199254740991]
    affine_cancel(over)^; print(over[0]); print("\n")
}
