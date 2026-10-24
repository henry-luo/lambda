// S7.1.1v3: exact UTF-8 and absence survive invariant read reuse.
pn matches(left: string, right: string, rows: int, columns: int) {
    var count = 0
    var i = 0
    while (i < rows) {
        var j = 0
        while (j < columns) {
            if (left[i] == right[j]) { count = count + 1 }
            j = j + 1
        }
        i = i + 1
    }
    return count
}
pn replace_text(var text) { text = "b" }
pn changed() {
    var left = "a"
    let right = "ab"
    var count = 0
    var j = 0
    while (j < 2) {
        if (left[0] == right[j]) { count = count + 1 }
        replace_text(left)
        j = j + 1
    }
    return count
}
pn main() {
    print([matches("aé🙂", "é🙂aé", 3, 4), matches("a", "a", 2, 2)]); print("\n")
    print([matches(chr(0), chr(0) ++ "a", 1, 2), matches("é", "é", 1, 2)]); print("\n")
    print([matches("", "", 0, 4), changed()]); print("\n")
}
