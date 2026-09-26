// LR03-19 (S1.6): a write to an object field lands in the field's own slot. A
// union- or range-typed field is a 9-byte self-describing slot that the flat
// 8-byte object layout overlapped with the next field, so a write to either
// clobbered the other. Golden written from the ruling, not from the runtime.
type Obj { a: int | string, b: string, c: int | float, d: 1 to 9, e: int }
pn main() {
    var o = <Obj a: 3, b: "b", c: 1.5, d: 2, e: 9>
    o.a = "str"
    o.c = 4
    o.e = 10
    print([o.a, o.b, o.c, o.d, o.e])
    o.b = "bb"
    o.d = 7
    print(o)
}
