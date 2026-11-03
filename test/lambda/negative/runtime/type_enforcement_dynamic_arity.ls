fn add(a: int, b: int) => a + b
fn dynamic_function() any { add }

pn main() {
    let target = dynamic_function();
    target(1)
}
