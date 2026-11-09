// static non-int carriers use their value helper; erased ints retain guarded speculation (D2.3.5, S4.4).
fn decimal_sum(a: decimal, b: decimal) any { return a + b }
fn decimal_remainder(a: decimal, b: decimal) any { return a % b }
fn decimal_quotient(a: decimal, b: decimal) decimal { return (a - a % b) / b }
fn decimal_chain(a: decimal, b: decimal) decimal {
    let shifted: decimal = a + b
    return decimal_quotient(shifted, b) + a
}
pn loose_decimal(a: any, b: any) any { return decimal_quotient(a, b) }
fn decimal_compare(a: decimal, b: decimal) any { return a < b }
fn string_compare(a: string, b: string) any { return a == b }
fn dynamic_sum(a: any, b: any) any { return a + b }
fn dynamic_product(a: any, b: any) any { return a * b }
fn nullable_relation(a: float?, b: any) any { return a < b }
pn decimal_text() any {
    var text: string = "v"
    text = text ++ 2.5m
    let saved = text
    text = text ++ 1.25m
    return [saved, text]
}
pn main() {
    print([decimal_sum(2.5m, 1.25m), decimal_remainder(8m, 3m),
        decimal_quotient(8m, 3m), decimal_compare(2.5m, 1.25m)]); print("\n")
    print([string_compare("x", "x"), string_compare("x", "y"),
        dynamic_sum(1, 2), dynamic_sum(2.5m, 1.25m)]); print("\n")
    let rejected = loose_decimal("bad", 3m)
    let nonfinite = decimal_quotient(1m, 0m)
    let propagated = decimal_sum(nonfinite, 1m)
    print([decimal_chain(8m, 3m), match rejected { case error: true default: false },
        match propagated { case decimal: true default: false }]); print("\n")
    print(decimal_text()); print("\n")
    print([dynamic_sum(2.5, 0.5), dynamic_product(2.5, 0.5),
        dynamic_product(3, 4), dynamic_product(null, 2.0)]); print("\n")
    print([nullable_relation(0.5, 1.0), nullable_relation(null, 1.0),
        nullable_relation(0.5, 1)]); print("\n")
}
