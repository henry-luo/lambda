// Object `pn` methods borrow a mutable receiver and write fields back to it.
type Counter {
    value: int = 0;
    pn increment() {
        value = value + 1
    }
    pn add(n: int) {
        value = value + n
    }
    fn double() => value * 2
}

pn main() {
    var default_counter = <Counter>
    default_counter.increment()
    default_counter.add(4)
    print(default_counter.value ++ " " ++ default_counter.double() ++ "\n")

    var literal_counter = <Counter value: 7>
    literal_counter.add(2)
    print(literal_counter.value ++ " " ++ literal_counter.double() ++ "\n")

    let original = <Counter value: 3>
    var changed = original
    changed.add(4)
    print(original.value ++ " " ++ changed.value ++ "\n")
}
