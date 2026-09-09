fn word_at(words, index) => if (index < len(words)) words[index] else ""
pn f_let(line) { let ws = split(line, " "); word_at(ws, 3) }
pn f_direct(line) { word_at(split(line, " "), 3) }
pn main() {
    let line = "section 12 records the base document with stable words for merging and review"
    var t0 = clock(); var n = 0; var i = 0
    while (i < 1000000) { n = n + len(f_let(line)); i = i + 1 }
    print("split let-bound 1M: " ++ string((clock() - t0) * 1000.0) ++ " ms " ++ string(n) ++ "\n")
    t0 = clock(); n = 0; i = 0
    while (i < 1000000) { n = n + len(f_direct(line)); i = i + 1 }
    print("split direct 1M: " ++ string((clock() - t0) * 1000.0) ++ " ms " ++ string(n) ++ "\n")
}
