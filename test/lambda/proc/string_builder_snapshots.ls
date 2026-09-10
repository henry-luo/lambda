// Snapshot publication must hold across branches, loop backedges and captures.
pn main() {
    var text: string = ""
    var saved: string = ""
    var history: array = []
    var i: int = 0
    while (i < 4) {
        history.push(text)
        text = text ++ "x"
        if (i == 1) { saved = text }
        else { i = i }
        i = i + 1
    }
    print(history); print("\n")
    print(saved); print("\n")
    print(text); print("\n")
    fn captured() => text
    let get_text = captured
    text = text ++ "!"
    print(get_text()); print("\n")
    print(text); print("\n")
    text = text ++ text
    print(text); print("\n")
    var unicode: string = ""
    var count: int = 0
    i = 0
    while (i < 4) {
        unicode = unicode ++ "é"
        count = count + len(unicode)
        i = i + 1
    }
    print([len(unicode), count, unicode]); print("\n")
    var binary_text: string = ""
    binary_text = binary_text ++ "a\u0000b" ++ "c"
    binary_text = binary_text ++ "\u0000"
    print([len(binary_text), len(slice(binary_text, 1, 3))]); print("\n")
}
