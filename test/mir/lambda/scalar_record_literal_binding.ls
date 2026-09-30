// D8.2.6, S9.1.2: a local exact record observed only through its fields
// needs no aggregate identity; an escaping record still materializes.
type Pair = {left: int, right: bool}

fn project(x: int) int {
    let pair: Pair = {left: x, right: true}
    if (pair.right) pair.left else 0
}

fn escape(x: int) Pair {
    let pair: Pair = {left: x, right: true}
    pair
}

fn checked(x: any) int {
    let pair: Pair = {left: x, right: true}
    pair.left
}

pn main() {
    print([project(7), project(-2), escape(5), checked(9)])
    print("\n")
}
