// S5.1.2/S11.4.1v3: a checked string result may still carry an error Item.
let words: string[] = ["red", "green"]

fn pick(index: int) string => words[index]

pn main() {
    let first = pick(0)
    let same = pick(0)
    let second = pick(1)
    let missing = pick(9)
    print([first == same, first != second, first == missing,
        missing == missing, pick(0) == pick(1)])
    print("\n")
}
