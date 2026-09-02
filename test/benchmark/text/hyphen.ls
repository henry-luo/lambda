// Text benchmark: bounded word hyphenation over mixed prose and HTML.
// Expected checksum: 731008 (matches the C port)
// This port keeps tags intact and applies deterministic vowel/consonant break
// points, which gives the C2MIR and Lambda ports the same text workload.

pn is_letter(ch) bool {
    let cp = ord(ch)
    return (cp >= 65 and cp <= 90) or (cp >= 97 and cp <= 122)
}

pn is_vowel(ch) bool {
    let cp = ord(ch)
    return cp == 65 or cp == 69 or cp == 73 or cp == 79 or cp == 85 or
        cp == 97 or cp == 101 or cp == 105 or cp == 111 or cp == 117
}

pn hyphenate(text) {
    var result = ""
    var i: int = 0
    var in_tag: int = 0
    let n = len(text)
    while (i < n) {
        let ch = text[i]
        if (ch == "<") {
            in_tag = 1
            result = result ++ ch
        } else if (ch == ">") {
            in_tag = 0
            result = result ++ ch
        } else {
            result = result ++ ch
            if (in_tag == 0 and i > 0 and i + 2 < n and
                is_letter(text[i - 1]) and is_letter(ch) and
                is_vowel(ch) and is_letter(text[i + 1]) and
                is_letter(text[i + 2]) and not is_vowel(text[i + 1])) {
                result = result ++ "-"
            }
        }
        i = i + 1
    }
    return result
}

pn main() {
    let texts: string[] = [
        "A certain king had a beautiful garden, and every morning he walked through it to admire the flowers.",
        "The tortoise never stopped for a moment, walking slowly but steadily right to the end of the course.",
        "A compiler transforms structured source text into executable instructions while preserving useful diagnostics.",
        "Text processing includes punctuation, capitalization, multiline paragraphs, and carefully selected exceptions.",
        "<article><h1>Hyphenation benchmark</h1><p>Beautiful documents require readable typography and consistent line breaking.</p></article>",
        "The algorithm combines a pattern trie with exception handling and a configurable hyphenation character."
    ]
    var checksum: int = 0
    var round: int = 0
    let t0 = clock()
    while (round < 32) {
        var index: int = 0
        while (index < len(texts)) {
            let result = hyphenate(texts[index])
            let result_length = len(result)
            checksum = checksum + result_length * 29
            if (result_length > 0) {
                checksum = checksum + ord(result[index % result_length])
            }
            if (round == 0) {
            }
            index = index + 1
        }
        round = round + 1
    }
    // 731008 is the checksum of the C reference port (test/benchmark/text/c2mir/hyphen.c);
    // a FAIL marker makes the runner exclude a wrong result instead of timing it
    if (checksum == 731008) {
        print("hyphen: CHECKSUM:" ++ checksum ++ "\n")
    } else {
        print("hyphen: FAIL checksum=" ++ checksum ++ "\n")
    }
    print("__TIMING__:" ++ ((clock() - t0) * 1000.0) ++ "\n")
}
