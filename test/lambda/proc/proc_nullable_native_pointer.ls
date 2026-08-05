// Nullable pointer-backed values use a raw NULL lane, in both shaped fields
// and native arrays, while a covariant source remains non-null.
type Row = {value: string?}

fn dynamic(value) any { value }

pn main() {
    var source = {value: "left"}
    var target: Row = source
    target.value = dynamic(null)
    var blank = target.value
    target.value = "right"

    var words: string[] = ["a", "b"]
    var optional_words: string?[] = words
    optional_words[1] = dynamic(null)
    print(string([source.value, target.value, blank, words[1], optional_words[0],
        optional_words[1], optional_words[2]]) ++ "\n")
}
