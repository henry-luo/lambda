// D2.2.2/D5.2.1v5: all exits determine a statement-bodied fn's return ABI.
fn fib(n: int) int {
    if (n < 2) { return n }
    return fib(n - 1) + fib(n - 2)
}
fn plain(n) {
    if (n < 2) { return n }
    return plain(n - 1) + plain(n - 2)
}
fn owned() {
    var record = {value: 3}
    return record
}
fn partial(flag, seed) {
    if (flag) { return float(seed) + 0.5 }
    7
}
fn routed(flag, seed) {
    if (flag) {
        let ignored = error("route") ^ { return float(seed) + 0.25 }
    }
    return owned()
}
// an uncertain caller must retain the complete boxed specialization fallback.
fn opaque(index: int) {
    let values = [42, "ok"]
    return values[index]
}
fn identity(value) { return value }
let known = identity("text")
let uncertain = identity(opaque(0))
let from_handler = routed(true, 4)
let from_partial = partial(true, 2)
let ordinary = partial(false, 2);
[fib(15) == 610, plain(15) == 610, owned().value == 3,
 from_handler is float, from_handler == 4.25, routed(false, 4).value == 3,
 from_partial is float, from_partial == 2.5, ordinary is int, ordinary == 7, known == "text", uncertain is int,
 uncertain == 42, len(identity(opaque(0))) == len(42)]
