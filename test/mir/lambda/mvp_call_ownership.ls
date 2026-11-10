// D6.2.1/D7.4.2/S9.1.3: native results, closure homes and argument snapshots.
fn increment(value: int) int => value + 1
fn forward(value) int^ => increment(value)^
fn wide(value: i64) i64^ => value + 1i64
fn make_adder(seed: int) {
    fn add(value: int) int => seed + value
    add
}
pn change(var values: array) { values[0] = 9; return 0 }
pn observe(values: array, ignored) { return values[0] }
pn main() {
    print([forward(8), forward("bad") is error]); print("\n")
    print(wide(2147483648i64)); print("\n")
    let add = make_adder(5)
    print(add(7)); print("\n")
    var values = [3]
    let old = observe(values, change(values))
    print([old, values[0]]); print("\n")
}
