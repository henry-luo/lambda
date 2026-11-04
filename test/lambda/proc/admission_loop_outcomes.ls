// SI14v3/D2.5.3: an admitted error remains safe across branch/loop joins.
fn opaque(value) => value
fn checked(value: int) int => value

// S7.7.3/SI14v3: aliases and captures retain an outcome exposed by a back edge.
pn alias_outcomes() {
    var count: int = 0
    var seen = []
    var step = 0
    while (step < 2) {
        let alias = count
        let result = checked(alias)
        let read = () any^ => alias
        let copied = read()
        seen = [*seen, result is error, alias is error, copied is error]
        count = opaque("bad")
        step = step + 1
    }
    seen
}

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
    [result is error, recovered, value is error, iteration, alias_outcomes()]
}
