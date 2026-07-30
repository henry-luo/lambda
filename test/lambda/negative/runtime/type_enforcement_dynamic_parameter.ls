fn accept(value: int) any { value }
fn dynamic_value() any { "not an integer" }

pn main() {
    accept(dynamic_value())
}
