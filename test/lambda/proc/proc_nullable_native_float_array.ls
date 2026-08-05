fn dynamic(value) any { value }

pn main() {
    var source: float[] = [1.5, 2.5]
    var target: float?[] = source
    target[1] = dynamic(null)
    var initially_null: float?[] = source
    initially_null[0] = dynamic(null)
    initially_null[0] = 1.25
    print(string([source[1], target[0], target[1], target[2],
        initially_null[0], initially_null[1]]) ++ "\n")
}
