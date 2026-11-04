// D6.2.1, D6.3.4: live strings survive collecting direct calls and loop backedges.
pn decorate(s: string, n: int) string { return s ++ ":" ++ n }
pn tiny(x: float) float { return x / 2.0 }
pn boxed(x: float) any { return x }
pn main() {
    var saved: string = decorate("root", 7)
    var current: string = ""
    var i: int = 0
    while (i < 80) {
        current = decorate(saved, i)
        i = i + 1
    }
    print(saved)
    print("\n")
    print(current)
    print("\n")
    let small = tiny(1e-310)
    print(small == 5e-311)
    print("\n")
    var a: any = boxed(1e-310)
    let b: any = a
    a = boxed(2e-310)
    print(b == 1e-310)
    print("\n")
}
