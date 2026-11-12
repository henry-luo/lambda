// S11.4.10: a semantic certificate never claims a named member's physical layout.
type Left = {a: int, b: string}
type Right = {b: string, c: int}
type Tagged = {tag: "good"} | {n: int}
fn consume(value: Left | Right) int => 1
fn tagged(value: Tagged) int => 1
pn main() {
    var value = parse("{\"b\":\"v\",\"a\":1}", 'json')^
    var total = 0
    for (i in 1 to 40) { total = total + consume(value) }
    let old = value
    value.a = "bad"
    let rejected = consume(value)
    value.a = 2
    let accepted = consume(value)
    var dependent = parse("{\"tag\":\"good\"}", 'json')^
    let first = tagged(dependent)
    dependent.tag = "bad"
    let second = tagged(dependent)
    print([total, rejected is error, accepted, old.a, value.a, first, second is error])
}
