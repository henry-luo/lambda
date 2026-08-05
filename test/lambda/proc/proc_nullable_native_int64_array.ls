fn dynamic(value) any { value }

pn main() {
    var source: i64[] = [1i64, 2i64]
    var target: i64?[] = source
    target[1] = dynamic(null)
    print(string([source[1], target[0], target[1], target[2]]) ++ "\n")
}
