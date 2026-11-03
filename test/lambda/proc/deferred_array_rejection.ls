pn main() {
    fn wrap(x) => x
    let xs = wrap([1, "x"])
    var ys: int[] = xs
    print([ys is error, "continued"])
}
