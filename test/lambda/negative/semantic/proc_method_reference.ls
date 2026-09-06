// S12.3.3v2 / D2.6.7: a pn method needs its mutable receiver write-back lane.
// A bound closure would retain only a snapshot, so it cannot be a value.
type Counter {
    value: int = 0,
    pn increment() {
        value = value + 1
    }
}

let counter = <Counter>
let deferred = counter.increment
