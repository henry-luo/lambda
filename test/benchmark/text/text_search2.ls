// T22-0 typed source: only search-function parameters carry int[] contracts.

let search_rounds = 1536
let modulus = 1000000007

pn build_corpus() {
    var rows = []
    var index = 0
    while (index < 512) {
        rows.push("record-" ++ string(index) ++
            " alpha aaaaaaaaaaaaaaaaaaaaaaaa token-" ++ string(index % 23) ++
            " omega needle-" ++ string(index % 11))
        index = index + 1
    }
    join(rows, "\n")
}

let patterns = [
    "record-0 alpha", "record-2048 alpha", "token-22 omega", "needle-10",
    "omega needle-7", "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3",
    "missing-marker", "record-2047 omega"
]

pn to_codes(text) {
    var codes = []
    var index = 0
    while (index < len(text)) {
        codes.push(ord(text[index]))
        index = index + 1
    }
    codes
}

pn naive_search(text: int[], pattern: int[], start: int) {
    if (len(pattern) == 0) { return start }
    var position = start
    while (position <= len(text) - len(pattern)) {
        var offset = 0
        while (offset < len(pattern) and text[position + offset] == pattern[offset]) {
            offset = offset + 1
        }
        if (offset == len(pattern)) { return position }
        position = position + 1
    }
    return -1
}

pn prefix_table(pattern: int[]) {
    var table = fill(len(pattern), 0)
    var length = 0
    var index = 1
    while (index < len(pattern)) {
        if (pattern[index] == pattern[length]) {
            length = length + 1
            table[index] = length
            index = index + 1
        } else if (length > 0) {
            length = table[length - 1]
        } else {
            index = index + 1
        }
    }
    table
}

pn kmp_search(text: int[], pattern: int[], start: int) {
    if (len(pattern) == 0) { return start }
    let table = prefix_table(pattern)
    var text_index = start
    var pattern_index = 0
    while (text_index < len(text)) {
        if (text[text_index] == pattern[pattern_index]) {
            text_index = text_index + 1
            pattern_index = pattern_index + 1
            if (pattern_index == len(pattern)) { return text_index - len(pattern) }
        } else if (pattern_index > 0) {
            pattern_index = table[pattern_index - 1]
        } else {
            text_index = text_index + 1
        }
    }
    return -1
}

pn boyer_moore_search(text: int[], pattern: int[], start: int) {
    if (len(pattern) == 0) { return start }
    var occurrences = fill(256, -1)
    var index = 0
    while (index < len(pattern) - 1) {
        occurrences[pattern[index]] = index
        index = index + 1
    }
    var position = start
    while (position <= len(text) - len(pattern)) {
        var offset = len(pattern) - 1
        while (offset >= 0 and text[position + offset] == pattern[offset]) {
            offset = offset - 1
        }
        if (offset < 0) { return position }
        let previous = occurrences[text[position + offset]]
        let shift = offset - previous
        position = position + if (shift > 1) shift else 1
    }
    return -1
}

pn main() {
    let corpus = build_corpus()
    let corpus_codes = to_codes(corpus)
    var pattern_codes = []
    var pattern_index = 0
    while (pattern_index < len(patterns)) {
        pattern_codes.push(to_codes(patterns[pattern_index]))
        pattern_index = pattern_index + 1
    }
    var checksum: int = 0
    let t0 = clock()
    var round: int = 0
    while (round < search_rounds) {
        var index: int = 0
        while (index < len(patterns)) {
            let start = (round * 17 + index * 13) % 97
            let naive = naive_search(corpus_codes, pattern_codes[index], start)
            let kmp = kmp_search(corpus_codes, pattern_codes[index], start)
            let boyer_moore = boyer_moore_search(corpus_codes, pattern_codes[index], start)
            if (naive != kmp or kmp != boyer_moore) {
                print("text_search: FAIL algorithm disagreement\n")
                return
            }
            checksum = (checksum + (naive + 2) * (index + 3) + (round + 1) * 7) % modulus
            index = index + 1
        }
        round = round + 1
    }
    if (checksum == 91395120) {
        print("text_search: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("text_search: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((clock() - t0) * 1000.0) ++ "\n")
}
