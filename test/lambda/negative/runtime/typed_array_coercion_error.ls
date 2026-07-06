pn main() {
    fn wrap(x) => x
    let xs = wrap([1, "x"])
    var ys: int[] = xs
    ys[0]
}
