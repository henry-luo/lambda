// S9.1.4v2/D7.2.2: escaping captures own snapshots in the shared closure ABI.
fn make_adder(base: int) {
    fn add(value: int = base) int { return base + value }
    return add
}
fn direct_capture(base: int) int {
    fn add(value: int) int { return base + value }
    return add(2)
}
fn make_reader(seed) {
    var saved_value = seed
    let read = () => saved_value
    saved_value = "changed"
    return read
}
fn make_container_reader() {
    var saved_value = [1, 2]
    let read = () => saved_value[0]
    saved_value[0] = 9
    return read
}
fn make_nested(base: int) { return (delta: int) => (value: int) => base + delta + value }
fn retain(value) { return () => value }
fn retain_outcome(value: any) {
    fn read() any { return value }
    return read
}
pn main() {
    let add = make_adder(7)
    print([add(), add(3), direct_capture(8)]); print("\n")
    let read = make_reader(4)
    let container = make_container_reader()
    print([read(), container()]); print("\n")
    let nested = make_nested(10)(3)
    print(nested(2)); print("\n")
    let callbacks = [for (index in [1, 2, 3]) () => index]
    print([callbacks[0](), callbacks[1](), callbacks[2]()]); print("\n")
    let cold = retain(1e-310)
    let churn = [retain(1e-300), retain(2.5)]
    print([cold() == 1e-310, churn[0]() == 1e-300, churn[1]() == 2.5]); print("\n")
    let read_error = retain_outcome(error("captured"))
    print(read_error() is error); print("\n")
}
