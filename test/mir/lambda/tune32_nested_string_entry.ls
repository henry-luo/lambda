// D8.3.1v2: nullable nested elements predict a string key with a boxed miss.
pn tune32_nested_same(left, right) bool {
    return left[0] == right[0]
}
pn main() {
    let pairs = [["ab", "ax"], ["é", "e"], ["😀", "😀"]]
    var i = 0
    while (i < 4) {
        print(tune32_nested_same(pairs[i][0], pairs[i][1])); print("\n")
        i = i + 1
    }
}
