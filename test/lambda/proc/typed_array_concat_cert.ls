// `acc ++ [x]` under a typed accumulator keeps the T[] certificate when every
// added element proves the element contract, so the next `acc: T[]` crossing
// admits in O(1); a mismatched element keeps the result uncertified, so it is
// no N[].
type N = {kind: int, kids: N[]}
let none: N[] = []
fn mk(k: int) N => {kind: k, kids: none}
fn build(acc: N[], k: int) N[] {
    if (k == 0) acc
    else build(acc ++ [mk(k)], k - 1)
}
fn total(xs: N[], i: int, sum: int) int {
    if (i >= len(xs)) sum
    else total(xs, i + 1, sum + xs[i].kind)
}
fn ints(acc: int[], k: int) int[] {
    if (k == 0) acc
    else ints(acc ++ [k], k - 1)
}
pn main() {
    let r = build([], 50)
    print(len(r)) print(" ") print(total(r, 0, 0)) print(" ")
    let loose = r ++ [{kind: 9, kids: none}]
    print(total(loose, 0, 0)) print(" ")
    let xs = ints([], 20)
    print(len(xs)) print(" ") print(xs[0] + xs[19]) print("\n")
    // a mismatched element must not ride the certificate into N[]
    let mixed = r ++ [{kind: 1}]
    print(r is N[]) print(" ") print(mixed is N[]) print("\n")
}
