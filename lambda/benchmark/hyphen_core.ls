// Text benchmark: Liang-pattern hyphenation over mixed prose and HTML.
// The en-US trie is mechanically extracted from hyphen.js before timing starts.

let hyphen_data_path = "test/benchmark/text/hyphen_patterns.json"
let hyphen_cases = [
    ["A certain king had a beautiful garden, and every morning he walked through it to admire the flowers.",
        "A cer-tain king had a beau-ti-ful gar-den, and every morn-ing he walked through it to ad-mire the flow-ers."],
    ["The tortoise never stopped for a moment, walking slowly but steadily right to the end of the course.",
        "The tor-toise nev-er stopped for a mo-ment, walk-ing slow-ly but steadi-ly right to the end of the course."],
    ["A compiler transforms structured source text into executable instructions while preserving useful diagnostics.",
        "A com-pil-er trans-forms struc-tured source text into ex-e-cutable in-struc-tions while pre-serv-ing use-ful di-ag-nos-tics."],
    ["Text processing includes punctuation, capitalization, multiline paragraphs, and carefully selected exceptions.",
        "Text pro-cess-ing in-cludes punc-tu-a-tion, cap-i-tal-iza-tion, mul-ti-line para-graphs, and care-ful-ly se-lect-ed ex-cep-tions."],
    ["<article><h1>Hyphenation benchmark</h1><p>Beautiful documents require readable typography and consistent line breaking.</p></article>",
        "<article><h1>Hy-phen-ation bench-mark</h1><p>Beau-ti-ful doc-u-ments re-quire read-able ty-pog-ra-phy and con-sis-tent line break-ing.</p></article>"],
    ["The algorithm combines a pattern trie with exception handling and a configurable hyphenation character.",
        "The al-go-rithm com-bines a pat-tern trie with ex-cep-tion han-dling and a con-fig-urable hy-phen-ation char-ac-ter."],
    ["associate associates declination obligatory philanthropic",
        "as-soc-iate as-soc-iates dec-lin-ati-on oblig-at-ory phil-ant-hropic"],
    ["recognizance reformation retribution reciprocity table present projects",
        "re-cogn-iza-nce ref-orm-ati-on ret-rib-uti-on reci-procity ta-ble present projects"],
    ["<article data-note=\"associate\">Hyphenation &amp; typography</article>",
        "<article data-note=\"associate\">Hy-phen-ation &amp; ty-pog-ra-phy</article>"],
    ["co-operate already-hyphenated exceptionally punctuation's boundary",
        "co-operate already-hyphenated ex-cep-tion-al-ly punc-tu-a-tion's bound-ary"],
    ["supercalifragilisticexpialidocious internationalization configuration",
        "su-per-cal-ifrag-ilis-tic-ex-pi-ali-do-cious in-ter-na-tion-al-iza-tion con-fig-u-ra-tion"],
    ["Configuration configuration configuration.",
        "Con-fig-u-ra-tion con-fig-u-ra-tion con-fig-u-ra-tion."],
    ["<3 is not markup, while <em>configuration</em> remains a word.",
        "<3 is not markup, while <em>con-fig-u-ra-tion</em> re-mains a word."]
]

pn is_ascii_letter(ch) bool {
    let cp = ord(ch)
    return (cp >= 65 and cp <= 90) or (cp >= 97 and cp <= 122)
}

pn is_word_char(ch) bool {
    return is_ascii_letter(ch) or ch == "'"
}

pn starts_html_tag(text, index) bool {
    return text[index] == "<" and index + 1 < len(text) and
        (is_ascii_letter(text[index + 1]) or text[index + 1] == "/")
}

pn trie_child(nodes, edges, node_index: int, code: int) int {
    let node = nodes[node_index]
    var offset: int = 0
    while (offset < node.count) {
        let edge = edges[node.first + offset]
        if (edge.code == code) {
            return edge.child
        }
        offset = offset + 1
    }
    return -1
}

pn markers_for_word(word, data, var marker_cache) {
    let lowered = lower(word)
    if (lowered at data.exceptions) {
        return data.exceptions[lowered]
    }
    if (lowered at marker_cache) {
        return marker_cache[lowered]
    }
    var levels: int[] = fill(len(word) + 1, 0)
    let padded = "." ++ lowered ++ "."
    var start: int = 0
    while (start + 2 < len(padded)) {
        var node_index: int = data.root
        var position: int = if (start == 0) 0 else start - 1
        var cursor: int = start
        while (cursor < len(padded)) {
            let child = trie_child(data.nodes, data.edges, node_index, ord(padded[cursor]))
            if (child < 0) {
                break
            }
            node_index = child
            let level_index = data.nodes[node_index].level
            if (level_index >= 0) {
                let level = data.levels[level_index]
                var level_offset: int = 0
                while (level_offset < len(level)) {
                    let target = position + level_offset
                    if (target >= 0 and target < len(levels) and level[level_offset] > levels[target]) {
                        levels[target] = level[level_offset]
                    }
                    level_offset = level_offset + 1
                }
            }
            cursor = cursor + 1
        }
        start = start + 1
    }
    levels[0] = 0
    levels[1] = 0
    levels[len(levels) - 1] = 0
    levels[len(levels) - 2] = 0
    var markers = []
    var index: int = 0
    while (index < len(levels)) {
        if (levels[index] % 2 == 1) {
            markers = markers ++ [index]
        }
        index = index + 1
    }
    marker_cache[lowered] = markers
    return markers
}

pn insert_hyphens(word, markers) string {
    var result = ""
    var marker_index: int = 0
    var index: int = 0
    while (index < len(word)) {
        if (marker_index < len(markers) and markers[marker_index] == index) {
            result = result ++ "-"
            marker_index = marker_index + 1
        }
        result = result ++ word[index]
        index = index + 1
    }
    while (marker_index < len(markers)) {
        result = result ++ "-"
        marker_index = marker_index + 1
    }
    return result
}

pn hyphenate_word(word, data, var result_cache, var marker_cache) string {
    if (word at result_cache) {
        return result_cache[word]
    }
    var result = word
    if (len(word) >= 5 and not contains(word, "-")) {
        result = insert_hyphens(word, markers_for_word(word, data, marker_cache))
    }
    result_cache[word] = result
    return result
}

pn hyphenate_text(text, data, var result_cache, var marker_cache) string {
    var result = ""
    var index: int = 0
    while (index < len(text)) {
        if (starts_html_tag(text, index)) {
            while (index < len(text)) {
                let ch = text[index]
                result = result ++ ch
                index = index + 1
                if (ch == ">") {
                    break
                }
            }
        } else if (is_word_char(text[index])) {
            var word = ""
            while (index < len(text)) {
                let ch = text[index]
                if (is_word_char(ch)) {
                    word = word ++ ch
                    index = index + 1
                } else if (ch == "-" and len(word) > 0 and index + 1 < len(text) and
                        is_ascii_letter(text[index + 1])) {
                    // retain explicit hyphens so the library's hyphen-character verifier sees one word.
                    word = word ++ ch
                    index = index + 1
                } else {
                    break
                }
            }
            result = result ++ hyphenate_word(word, data, result_cache, marker_cache)
        } else {
            result = result ++ text[index]
            index = index + 1
        }
    }
    return result
}

pn verify_hyphen_cases(data) bool {
    var result_cache = {}
    var marker_cache = {}
    var index: int = 0
    while (index < len(hyphen_cases)) {
        if (hyphenate_text(hyphen_cases[index][0], data, result_cache, marker_cache) != hyphen_cases[index][1]) {
            return false
        }
        index = index + 1
    }
    return true
}

pub pn run_hyphen_benchmark() {
    let data = input(hyphen_data_path, {type: "json"}) ^ { null }
    if (data == null or not verify_hyphen_cases(data)) {
        print("hyphen: FAIL fixture verification\n")
        return
    }
    var texts = []
    var text_index: int = 0
    while (text_index < len(hyphen_cases)) {
        texts = texts ++ [hyphen_cases[text_index][0]]
        text_index = text_index + 1
    }
    var checksum: int = 0
    var round: int = 0
    let t0 = clock()
    while (round < 32) {
        var result_cache = {}
        var marker_cache = {}
        var index: int = 0
        while (index < len(texts)) {
            let result = hyphenate_text(texts[index], data, result_cache, marker_cache)
            checksum = (checksum + len(result) * 29) % 1000000007
            if (len(result) > 0) {
                checksum = (checksum + ord(result[index % len(result)])) % 1000000007
            }
            index = index + 1
        }
        round = round + 1
    }
    if (checksum == 1183296) {
        print("hyphen: CHECKSUM:" ++ checksum ++ "\n")
    } else {
        print("hyphen: FAIL checksum=" ++ checksum ++ "\n")
    }
    print("__TIMING__:" ++ ((clock() - t0) * 1000.0) ++ "\n")
}
