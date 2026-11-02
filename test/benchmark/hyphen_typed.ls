// typed Liang trie traversal; text stays in strings throughout the pipeline.
import .hyphen_tables
import .hyphen_common

type WordCache = {keys: string[], values: string[]}
type MarkerCache = {keys: string[], values: int[][]}

pn word_cache_new() WordCache {
    return {keys: [], values: []}
}

pn marker_cache_new() MarkerCache {
    return {keys: [], values: []}
}

pn find_key(keys: string[], word: string) int {
    var index: int = 0
    while (index < len(keys)) {
        if (keys[index] == word) { return index }
        index = index + 1
    }
    return -1
}

pn trie_child(tables: HyphenTables, node: int, code: int) int^ {
    let _admitted_first_26: int = tables.node_first[node]
    let first: int = _admitted_first_26^
    let _admitted_count_27: int = tables.node_count[node]
    let count: int = _admitted_count_27^
    var offset: int = 0
    while (offset < count) {
        let edge: int = first + offset
        if (tables.edge_code[edge] == code) { return tables.edge_child[edge] }
        offset = offset + 1
    }
    return -1
}

pn markers_for_word(tables: HyphenTables, word: string, var cache: MarkerCache) int[]^ {
    let lowered: string = lower(word)
    let exception: int = find_key(tables.exception_words, lowered)
    if (exception >= 0) {
        let _admitted_result_41: int[] = []
        var result: int[] = _admitted_result_41
        let _admitted_first_42: int = tables.exception_offsets[exception]
        let first: int = _admitted_first_42^
        let _admitted_count_43: int = tables.exception_counts[exception]
        let count: int = _admitted_count_43^
        var index: int = 0
        while (index < count) {
            push(result, tables.exception_markers[first + index])^
            index = index + 1
        }
        return result
    }
    let cached: int = find_key(cache.keys, lowered)
    if (cached >= 0) { return cache.values[cached] }

    let length: int = len(word)
    var levels: int[] = fill(length + 1, 0)^
    var span_start: int = 0
    while (span_start < length) {
        var node: int = tables.root
        let position: int = if (span_start == 0) 0 else span_start - 1
        var cursor: int = span_start
        while (cursor < length + 2) {
            // virtual boundary dots avoid building a padded copy of each word.
            let _admitted_code_63: int = if (cursor == 0 or cursor == length + 1) 46
                else ord(lowered[cursor - 1])
            let code: int = _admitted_code_63^
            let child: int = trie_child(tables, node, code)^
            if (child < 0) { break }
            node = child
            let _admitted_level_68: int = tables.node_level[node]
            let level: int = _admitted_level_68^
            if (level >= 0) {
                let _admitted_first_70: int = tables.level_offsets[level]
                let first: int = _admitted_first_70^
                let _admitted_count_71: int = tables.level_lengths[level]
                let count: int = _admitted_count_71^
                var offset: int = 0
                while (offset < count) {
                    let target: int = position + offset
                    let _admitted_value_75: int = tables.level_values[first + offset]
                    let value: int = _admitted_value_75^
                    if (target >= 0 and target <= length and value > levels[target]) {
                        levels[target] = value
                    }
                    offset = offset + 1
                }
            }
            cursor = cursor + 1
        }
        span_start = span_start + 1
    }
    levels[0] = 0
    levels[1] = 0
    levels[length] = 0
    levels[length - 1] = 0
    let _admitted_markers_90: int[] = []
    var markers: int[] = _admitted_markers_90
    var index: int = 0
    while (index <= length) {
        if (levels[index] % 2 == 1) { push(markers, index)^ }
        index = index + 1
    }
    push(cache.keys, lowered)^
    push(cache.values, markers)
    return markers
}

pn insert_hyphens(word: string, markers: int[]) string^ {
    var result: string = ""
    var span_start: int = 0
    var index: int = 0
    while (index < len(markers)) {
        let _admitted_end_106: int = markers[index]
        let end: int = _admitted_end_106^
        // S7.1.2: slice has an exclusive end; copy one span per hyphen.
        let _admitted_result_108: string = result ++ slice(word, span_start, end) ++ "-"
        result = _admitted_result_108^
        span_start = end
        index = index + 1
    }
    return result ++ slice(word, span_start, len(word))
}

pn hyphenate_word(tables: HyphenTables, word: string, var results: WordCache, var markers: MarkerCache) string^ {
    let cached: int = find_key(results.keys, word)
    if (cached >= 0) { return results.values[cached] }
    var result: string = word
    if (len(word) >= 5 and not contains(word, "-")) {
        result = insert_hyphens(word, markers_for_word(tables, word, markers))^
    }
    push(results.keys, word)^
    push(results.values, result)
    return result
}

pn hyphenate_text(tables: HyphenTables, text: string, var results: WordCache, var markers: MarkerCache) string^ {
    var result: string = ""
    var index: int = 0
    var unchanged_start: int = 0
    let length: int = len(text)
    while (index < length) {
        if (starts_html_tag(text, index)^) {
            while (index < length) {
                let _admitted_ch_135: string = text[index]
                let ch: string = _admitted_ch_135^
                index = index + 1
                if (ch == ">") { break }
            }
        } else if (is_word_char(text[index])^) {
            let word_start: int = index
            while (index < length) {
                let _admitted_ch_142: string = text[index]
                let ch: string = _admitted_ch_142^
                if (is_word_char(ch)^) {
                    index = index + 1
                } else if ((ch == "-" and index > word_start and index + 1 < length and
                        is_ascii_letter(text[index + 1]))^) {
                    index = index + 1
                } else { break }
            }
            let word: string = (slice(text, word_start, index) ^ { null }) or ""
            let _admitted_result_151: string = result ++ slice(text, unchanged_start, word_start) ++
                hyphenate_word(tables, word, results, markers)
            result = _admitted_result_151^
            unchanged_start = index
        } else {
            index = index + 1
        }
    }
    return result ++ slice(text, unchanged_start, length)
}

pub pn hyphenate(tables: HyphenTables, text: string) string^ {
    var results: WordCache = word_cache_new()^
    var markers: MarkerCache = marker_cache_new()^
    return hyphenate_text(tables, text, results, markers)
}

pub pn run_hyphen_benchmark() {
    let tables: HyphenTables = load_hyphen_tables()^
    let _admitted_cases_169: string[][] = hyphen_cases
    let cases: string[][] = _admitted_cases_169^
    var verify_results: WordCache = word_cache_new()^
    var verify_markers: MarkerCache = marker_cache_new()^
    var index: int = 0
    while (index < len(cases)) {
        if ((hyphenate_text(tables, cases[index][0], verify_results, verify_markers) != cases[index][1])^) {
            print("hyphen: FAIL fixture verification\n")
            return
        }
        index = index + 1
    }
    let _admitted_texts_180: string[] = []
    var texts: string[] = _admitted_texts_180
    index = 0
    while (index < len(cases)) {
        push(texts, cases[index][0])^
        index = index + 1
    }
    var checksum: int = 0
    var round_index: int = 0
    let t0 = clock()
    while (round_index < 32) {
        // retain the canonical per-round cold caches and S9.1.3 var borrows.
        var results: WordCache = word_cache_new()^
        var markers: MarkerCache = marker_cache_new()^
        index = 0
        while (index < len(texts)) {
            let result: string = hyphenate_text(tables, texts[index], results, markers)^
            checksum = (checksum + len(result) * 29) % 1000000007
            if (len(result) > 0) {
                let _admitted_checksum_198: int = (checksum + ord(result[index % len(result)])) % 1000000007
                checksum = _admitted_checksum_198^
            }
            index = index + 1
        }
        round_index = round_index + 1
    }
    if (checksum == 1183296) {
        print("hyphen: CHECKSUM:" ++ checksum ++ "\n")
    } else {
        print("hyphen: FAIL checksum=" ++ checksum ++ "\n")
    }
    print("__TIMING__:" ++ ((clock() - t0) * 1000.0) ++ "\n")
}
