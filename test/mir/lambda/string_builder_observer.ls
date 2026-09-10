// Length observes the buffer without publishing an immutable alias (S1.6).
pn observe_builder() int {
    var text: string = ""
    var checksum: int = 0
    var i: int = 0
    while (i < 128) {
        text = text ++ "x"
        checksum = checksum + len(text)
        i = i + 1
    }
    return checksum + len(text)
}

pn main() { print(observe_builder()) }
