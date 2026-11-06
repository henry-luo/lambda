// S12.3.3v2/S9.3.1: a saved fn method captures its receiver by value;
// a later pn method updates the caller's detached receiver.
type Counter {
    value: int,
    fn read() => value
    pn increment() { value = value + 1 }
}
pn main() {
    var counter = <Counter value: 7>
    let saved = counter.read
    counter.increment()^;
    [saved() == 7, call(saved, []) == 7, counter.read() == 8]
}
