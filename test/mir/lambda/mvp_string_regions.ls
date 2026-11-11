// S9.1.2/D8.2.6: native builders preserve observations and checked character reads.
pn typed_build(count: int) string {
    var text = ""
    var i: int = 0
    while (i < count) { text = text ++ "x"; i = i + 1 }
    return text
}
pn erased_build(count) {
    var text = ""
    var i = 0
    while (i < count) { text = text ++ "x"; i = i + 1 }
    return text
}
pn frozen(count: int) {
    var text = "s"
    let before = text
    var i: int = 0
    while (i < count) { text = text ++ "x"; i = i + 1 }
    return [len(text), before]
}
fn first_is_x(value: string) bool => value[0] == "x"
pn main() {
    print([len(typed_build(0)), len(typed_build(12)), len(erased_build(0)), len(erased_build(12))]); print("\n")
    print(frozen(12)); print("\n")
    print([first_is_x("x"), first_is_x("y"), first_is_x(""), first_is_x("é")]); print("\n")
}
