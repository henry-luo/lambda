// LR07-15 (write half): a `pn` method's writes to implicit receiver fields
// must reach the caller's binding. The method prologue snapshots fields into
// locals and the epilogue writes them back through the receiver; the write-back
// is keyed on the field's binding, which was NULL for object fields, so on the
// eager JIT tier the mutation was silently dropped.

type Counter {
    value: int,
    fn get() => value
    pn bump() { value = value + 1 }
    pn add(n: int) { value = value + n }
    fn get_explicit() => ~.value
    pn bump_explicit() { ~.value = ~.value + 1 }
    pn mixed() {
        ~.value = value + 1
        value = ~.value + 1
        ~.value = ~.value + 1
        return
    }
}

// D3.1.4: explicit-only fields still need indexed receiver locals.
type ExplicitCounter {
    count: int = 0
    pn bump() { ~.count = ~.count + 1 }
    fn get() => ~.count
}

pn main() {
    var c = <Counter value: 5>
    c.bump()^
    print(c.value); print("\n")
    c.bump()^
    print(c.get()); print("\n")
    c.add(10)^
    print(c.value); print(" "); print(c.get()); print("\n")
    let saved = c
    c.bump_explicit()^
    print([c.value, c.get_explicit(), saved.value]); print("\n")
    c.mixed()^
    print([c.value, c.get_explicit(), saved.value]); print("\n")
    var explicit = <ExplicitCounter>
    explicit.bump()^
    print([explicit.count, explicit.get()]); print("\n")
}
