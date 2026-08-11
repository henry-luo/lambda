// Braced for-expression content inherits its enclosing procedure context.
pn main() {
    let transformed = for (x in [1, 2, 3]) {
        var value = x * 2;
        value = value + 1;
        value
    }
    print(transformed)
}
