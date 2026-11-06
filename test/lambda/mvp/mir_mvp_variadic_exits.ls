// S7.7.3/S16.9.7: admission and early completion restore the enclosing rest view.
pn first(stop, ...) {
    if (stop) { return varg() }
    return []
}
pn checked(value: int, ...) { return value }
pn outer(input: any, ...) {
    let before = varg()
    let early = first(true, "inner", null)
    let empty = first(false)
    let rejected = checked(input, 1, 2)
    let allocate = [for (i in 1 to 40) {text: string(i)}]
    return [early == ["inner", null], empty == [], rejected is error,
        varg() == before, varg(0) == "outer", len(allocate) == 40]
}
pn borrow(var value: any, ...) {
    value = [value, varg()]
    return varg(0)
}
pn main() {
    var value: any = 3
    let borrowed = borrow(value, null, "a", "b")
    return [outer("bad", "outer", [1, 2]), value == [3, [null, "a", "b"]], borrowed == null]
}
