// @expect-error: E201
pn main() {
    var rejected: bool[] = [true, "no"]
    print(rejected)
}
