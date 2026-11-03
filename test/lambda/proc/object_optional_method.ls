// S16.9.5: read-only methods and untaken writes keep optional fields absent.
type R {
    name: string, value?: int?, tail: int = 4,
    fn read() => [name, value, tail]
    pn set(flag: bool, v: int?) { if (flag) { value = v } }
}
type Counter {
    value: int,
    fn read() => value
    pn step() { value = value + 1 }
}
pn main() {
    var r = <R name: "r">
    r.read()^
    r.set(false, 9)^
    print(['value' at r, len(r), r.value]); print("\n")
    let before = r
    r.set(true, null)^
    print(['value' at r, len(r), r.value, 'value' at before, len(before)]); print("\n")
    r.set(true, 7)^
    print(r.read()); print("\n")
    var counter = <Counter value: 1>
    let saved = counter
    counter.step()^
    print([saved.value, counter.value]); print("\n")
}
