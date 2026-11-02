// SI14v3/D2.8.3v3: an admitted error remains safe across branch/loop joins.
fn opaque(value) => value
pn main() {
    var value: int = 1
    var iteration = 0
    var result: any = null
    while (iteration < 3) {
        result = value + 1
        value = opaque("bad")
        iteration = iteration + 1
    }
    if (value is error) { value = opaque(9) }
    let recovered = value + 1
    value = opaque("last failure");
    [result is error, recovered, value is error, iteration]
}
