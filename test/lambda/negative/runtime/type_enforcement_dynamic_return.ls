fn dynamic_value() any { "not an integer" }
fn declared_result() int { dynamic_value() }

pn main() {
    declared_result()
}
