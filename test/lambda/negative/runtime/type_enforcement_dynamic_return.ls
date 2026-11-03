fn opaque(value) => value
fn dynamic_value() => opaque("not an integer")
fn declared_result() int { dynamic_value() }

pn main() {
    declared_result()
}
