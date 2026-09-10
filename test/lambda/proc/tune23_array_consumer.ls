fn word(words: string[], index: int) string =>
    if (index < len(words)) words[index] else ""

pn main() {
    let words = split("alpha beta gamma", " ")
    var total = 0
    var index = 0
    while (index < 20) {
        total = total + len(word(words, index % len(words)))
        index = index + 1
    }
    // an open alias must retain its own carrier and mutation contract.
    let open_words = split("one two", " ")
    let first = word(open_words, 0)
    var alias = open_words
    alias.push(42)
    print([total, first, len(open_words), alias[2]])
}
