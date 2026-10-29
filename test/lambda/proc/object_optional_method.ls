// S16.9.5: read-only methods and untaken writes keep optional fields absent.
type R {
    name: string, value?: int?, tail: int = 4,
    fn read() => [name, value, tail]
    pn set(flag: bool, v: int?) { if (flag) { value = v } }
}
pn main() {
    var r = <R name: "r">
    r.read()
    r.set(false, 9)
    print(['value' at r, len(r), r.value]); print("\n")
    let before = r
    r.set(true, null)
    print(['value' at r, len(r), r.value, 'value' at before, len(before)]); print("\n")
    r.set(true, 7)
    print(r.read()); print("\n")
}
