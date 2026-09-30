// D2.2.2/S4.1.2: both dominating bounds exclude int poison and keep x + 1
// inside int53. One-sided tests and facts outside their arm retain slow paths.
fn bounded(x: int) int => if (x >= 0 and x < 10) x + 1 else 0

fn reversed(x: int) int => if (0 <= x and 10 > x) x + 1 else 0

fn unbounded(x: int) int => if (x >= 0) x + 1 else 0

fn leaked(x: int) int => if (x >= 0 and x < 10) 0 else x + 1

fn equal(x: int) int => if (x == 9) x + 1 else 0

pn bounded_local(x: int) int {
    let stable: int = x
    if (stable >= 0 and stable < 10) { return stable + 1 }
    return 0
}

pn bounded_param(x: int) int {
    if (x >= 0 and x < 10) { return x + 1 }
    return 0
}

pn rebound_param(x: int) int {
    if (x >= 0 and x < 10) {
        x = 9007199254740991
        return x + 1
    }
    return 0
}

pn main() {
    print(bounded(9)); print(" ")
    print(reversed(9)); print(" ")
    print(unbounded(9)); print(" ")
    print(leaked(9)); print(" ")
    print(equal(9)); print(" ")
    print(bounded_local(9)); print(" ")
    print(bounded_param(9)); print(" ")
    print(rebound_param(9)); print("\n")
}
