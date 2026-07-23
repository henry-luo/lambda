// A mutating object method needs an assignable receiver root.
type Counter {
    value: int = 0;
    pn increment() {
        value = value + 1
    }
}

pn main() {
    let frozen = <Counter>
    frozen.increment()
}
