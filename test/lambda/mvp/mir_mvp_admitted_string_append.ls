// S7.7.2v2, S9.1.2: a proven admission temporary permits fusion without losing snapshots.
fn number_text(n: int) string { return string(n) }
fn wrap(text: string) string { return "<" ++ text ++ ">" }

pn append() string {
    var text: string = "prefix"
    var part: string = wrap(number_text(7))
    let admitted: string = text ++ part
    text = admitted
    return text
}

pn observed() any {
    var text: string = "prefix"
    let saved = text
    var part: string = wrap(number_text(8))
    let admitted: string = text ++ part
    text = admitted
    return [saved, text, part]
}

pn main() {
    print(append()); print("\n")
    print(observed()); print("\n")
}
