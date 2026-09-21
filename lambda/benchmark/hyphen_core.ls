// Text benchmark: Liang-pattern hyphenation over mixed prose and HTML.
// The en-US trie is mechanically extracted from hyphen.js before timing starts.

let hyphen_data_path = "test/benchmark/text/hyphen_patterns.json"
import test.benchmark.hyphen_common

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
