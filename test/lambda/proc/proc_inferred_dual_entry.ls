// An escaped untyped function may infer an exact raw shape for direct calls,
// but indirect and mismatched calls must keep the source-equivalent boxed lane.
fn add_one(value) { value + 1 }
fn exposed() any { add_one }
fn dynamic(value) any { value }
fn double(value) { value + value }
fn exposed_double() any { double }

pn main() {
    let boxed = exposed()
    let direct_wide = double(4503599627370496)
    let boxed_double = exposed_double()
    let wide = boxed_double(4503599627370496)
    let fractional = boxed_double(2.5)
    print(string([add_one(2), add_one(dynamic(2.5)), boxed(2), boxed(2.5), double(1),
        direct_wide is float, direct_wide == 9007199254740992.0,
        wide is float, wide == 9007199254740992.0, fractional]) ++ "\n")
}
