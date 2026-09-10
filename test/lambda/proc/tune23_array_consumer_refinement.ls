type Word = "alpha"
fn first(words: Word[]) any => words[0]
pn attempt() any^ {
    let words = split("alpha beta", " ")
    print("evaluated\n")
    return first(words)
}
fn first_string(words: string[]) any => words[0]
pn absent(text: string?) any^ {
    let words = split(text, " ")
    print("nullable evaluated\n")
    return first_string(words)
}
type Node = {value: int} | {text: string}
fn take(node: Node) int => 1
pn invalid_union() any^ {
    let input = {value: "wrong"}
    return take(input)
}
pn numeric(values: int[]) int { return len(values) }
pn invalid_numeric() any^ {
    let values = [1.5]
    return numeric(values)
}
pn main() {
    attempt() ^ { print("caught\n") }
    absent(null) ^ { print("nullable caught\n") }
    invalid_union() ^ { print("union caught\n") }
    invalid_numeric() ^ { print("numeric caught\n") }
}
