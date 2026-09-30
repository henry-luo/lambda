// D4.4.4v4: a collecting call relocates the packed lane, but an immutable
// admitted array keeps its logical length (S9.1.2, S7.1.3v2).
pn stable_length(values: int[], text: string) int {
    let before = len(values)
    let words = split(text, ",")
    return before + len(values) + values[0] + len(words)
}

pn changing_length(var values: int[], text: string) int {
    let words = split(text, ",")
    values.push(len(words))
    return len(values) + values[0]
}

pn resize_local(values: int[], text: string) int {
    let before = len(values)
    let words = split(text, ",")
    values.push(len(words))
    return before + len(values) + values[0]
}

pn main() {
    let a: int[] = [3, 4]
    var b: int[] = [3, 4]
    print([stable_length(a, "x,y"), changing_length(b, "x,y"),
        resize_local(a, "x,y")])
}
