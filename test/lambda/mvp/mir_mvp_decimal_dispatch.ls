// typed arithmetic retains the shared decimal and BigInt rules (S4.4, D5.3).
fn decimal_ops(a: decimal, b: decimal) any {
    return [a + b, a - b, a * b, a / b, a div b, a % b]
}

fn decimal_typed(a: decimal, b: decimal) decimal { return (a + b) * b - a }
fn decimal_generic(a: any, b: any) any { return (a + b) * b - a }
pn decimal_dynamic(a: any) any { return decimal_typed(a, 3m) }

pn main() {
    print(decimal_ops(12m, 3m)); print("\n")
    print(decimal_ops(12n, 3n)); print("\n")
    let huge = 1000000000000000000000000000001n
    print(decimal_typed(huge, 7n) == decimal_generic(huge, 7n)); print("\n")
    let rejected = decimal_dynamic("bad")
    print(rejected is error); print("\n")
}
