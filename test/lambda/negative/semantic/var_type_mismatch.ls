// Negative test: type-annotated var assigned incompatible type
// Expected error: E201 - cannot assign string to int var

pn main() {
    var x: int = 42
    x = "hello"
    print(x)
}
