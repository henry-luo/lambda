// Fresh expressions batch copies; nullable leaves retain the original join tree.
fn expression_builder(a: string, b: string) string => a ++ ":" ++ b ++ "!"

fn indexed_builder(parts: string[], i: int) string =>
    "[" ++ (parts[i] ++ parts[i + 1]) ++ "]"

pn marked_piece(value: string) string {
    print(value)
    return value
}

pn main() {
    print(expression_builder("A", "B")); print("\n")
    let parts: string[] = ["a", "b"]
    print(indexed_builder(parts, 0)); print("\n")
    print(indexed_builder(parts, 1)); print("\n")
    print(indexed_builder(parts, 2)); print("\n")
    var text: string = ""
    var i: int = 0
    while (i < 3) {
        text = text ++ parts[0] ++ parts[1] ++ "."
        i = i + 1
    }
    print(text); print("\n")
    print(marked_piece("1") ++ (marked_piece("2") ++ marked_piece("3")))
    print("\n")
}
