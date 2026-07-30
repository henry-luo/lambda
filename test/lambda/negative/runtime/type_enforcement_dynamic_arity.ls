fn add(a: int, b: int) => a + b
fn dynamic_function() any { add }

pn main() {
    dynamic_function()(1)
}
