fn first(words: string[]) string => words[0]

pn main() {
    var parts = split("alpha beta", " ")
    parts.push(7)
    let typed = split("gamma delta", " ")
    print([parts, first(typed)])
}
