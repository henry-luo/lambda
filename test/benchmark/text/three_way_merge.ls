// Text benchmark: line-level and word-level three-way merge of related texts.

let merge_rounds = 11000
let line_count = 768
let modulus = 1000000007

pn build_base() {
    var lines = []
    var index = 0
    while (index < line_count) {
        lines.push("section " ++ string(index) ++
            " records the base document with stable words for merging and review")
        index = index + 1
    }
    lines
}

pn make_variant(base, side) {
    var lines = []
    var index = 0
    while (index < len(base)) {
        var line = base[index]
        if (index % 17 == 0) {
            line = line ++ " " ++ side ++ " edit " ++ string(index % 31) ++
                " keeps the paragraph useful"
        } else if (side == "left" and index % 23 == 0) {
            line = line ++ " left-only annotation"
        } else if (side == "right" and index % 29 == 0) {
            line = line ++ " right-only annotation"
        }
        lines.push(line)
        index = index + 1
    }
    lines
}

fn word_at(words, index) => if (index < len(words)) words[index] else ""

pn merge_words(base_line, left_line, right_line) {
    if (left_line == right_line) { return left_line }
    if (left_line == base_line) { return right_line }
    if (right_line == base_line) { return left_line }
    let base_words = split(base_line, " ")
    let left_words = split(left_line, " ")
    let right_words = split(right_line, " ")
    var count = len(base_words)
    if (len(left_words) > count) { count = len(left_words) }
    if (len(right_words) > count) { count = len(right_words) }
    var words = []
    var index = 0
    while (index < count) {
        let base_word = word_at(base_words, index)
        let left_word = word_at(left_words, index)
        let right_word = word_at(right_words, index)
        if (left_word == right_word) words.push(left_word)
        else if (left_word == base_word) words.push(right_word)
        else if (right_word == base_word) words.push(left_word)
        else {
            words.push("<<<<<<< LEFT")
            words.push(left_word)
            words.push("=======")
            words.push(right_word)
            words.push(">>>>>>> RIGHT")
        }
        index = index + 1
    }
    join(words, " ")
}

pn merge_lines(base_lines, left_lines, right_lines) {
    var merged = []
    var index = 0
    while (index < len(base_lines)) {
        let base_line = base_lines[index]
        let left_line = left_lines[index]
        let right_line = right_lines[index]
        if (left_line == right_line) merged.push(left_line)
        else if (left_line == base_line) merged.push(right_line)
        else if (right_line == base_line) merged.push(left_line)
        else merged.push(merge_words(base_line, left_line, right_line))
        index = index + 1
    }
    join(merged, "\n")
}

pn main() {
    let base = build_base()
    let left = make_variant(base, "left")
    let right = make_variant(base, "right")
    var checksum: int = 0
    let t0 = clock()
    var round: int = 0
    while (round < merge_rounds) {
        let merged = merge_lines(base, left, right)
        checksum = (checksum + len(merged) * 31 + ord(merged[(round * 37) % len(merged)])) % modulus
        round = round + 1
    }
    if (checksum == 342313356) {
        print("three_way_merge: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("three_way_merge: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((clock() - t0) * 1000.0) ++ "\n")
}
