// S11.1.6v2 + S11.4.5: `T?` is `T | null`, so a nullable float contract admits
// an int exactly as its base `float` does -- re-represented as a float, on
// every tier. The JIT failed MIR verification on `let x: float? = 5` (an int
// `add` on the binding's double register), and T0 kept `5` an int in a
// `float | null` slot where the JIT's nullable lane stored 5.0.

let m1: float? = 5
let m2: float | null = 5
let m3: float = 5
fn widen(a: float | null) { [a, type(a)] }
fn opt(a: float?) { a }
fn dyn(v) => v

pn main() {
    // module level, and a use of each binding
    print([m1, type(m1), m1 + 1, m2, type(m2), m3, type(m3)])
    print("\n")

    // local declarations
    let x: float? = 5
    let y: float | null = 5
    let z: float = 5
    print([x, type(x), x * 2, y, type(y), z, type(z)])
    print("\n")

    // parameters
    print([widen(5), widen(2.5), widen(null), opt(7), type(opt(7))])
    print("\n")

    // reassignment of a declared `float | null`
    var r: float | null = null
    r = 5
    print([r, type(r)])
    r = null
    print([r])
    print("\n")

    // a dynamic value is admitted the same way, and `int?` takes an exact float
    let d: float? = dyn(3)
    let i: int? = dyn(2.0)
    print([d, type(d), i, type(i)])
    print("\n")

    // a union of several numeric arms admits by membership: `5` stays an int
    let u: int | float | null = 5
    print([u, type(u)])
    print("\n")
}
