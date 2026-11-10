// S10.6.1/S9.1.2: an owned prefix is consumable only after observers freeze it.
fn erased_append(prefix: any, tail: any) any {
    var text = prefix
    text = text ++ tail
    return text
}
pn transferred() any^ {
    var text: string = ""
    var observed: array = []
    var index = 0
    while (index < 3) {
        observed.push(text)^
        let admitted: string = text ++ "x"
        text = admitted
        index = index + 1
    }
    return [observed, text]
}
fn retained_temporary() any {
    var text: string = "a"
    let admitted: string = text ++ "b"
    let saved = admitted
    text = admitted
    text = text ++ "c"
    return [saved, text]
}
pn main() {
    print([erased_append("a", "b"), erased_append(null, "b"), erased_append("a", null), erased_append(null, null)]); print("\n")
    print([erased_append([1], [2]), erased_append("a", 7)]); print("\n")
    print(transferred()^); print("\n")
    print(retained_temporary()); print("\n")
}
