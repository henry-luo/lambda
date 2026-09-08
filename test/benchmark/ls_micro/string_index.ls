pn main() {
    var lines = []
    var i = 0
    while (i < 768) { lines.push("section " ++ string(i) ++ " records the base document with stable words for merging and review"); i = i + 1 }
    let joined = join(lines, "\n")
    var t0 = clock(); var s = 0; var r = 0
    while (r < 11000) { s = s + ord(joined[(r * 37) % len(joined)]); r = r + 1 }
    print("index joined string x11000: " ++ string((clock() - t0) * 1000.0) ++ " ms " ++ string(s) ++ "\n")
    var acc = ""
    i = 0
    while (i < 768) { acc = acc ++ "section " ++ string(i) ++ " records the base document with stable words for merging and review\n"; i = i + 1 }
    t0 = clock(); s = 0; r = 0
    while (r < 11000) { s = s + ord(acc[(r * 37) % len(acc)]); r = r + 1 }
    print("index ++-built string x11000: " ++ string((clock() - t0) * 1000.0) ++ " ms " ++ string(s) ++ "\n")
}
