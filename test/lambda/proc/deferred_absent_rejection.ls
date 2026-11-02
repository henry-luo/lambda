// S7.7.2v2: strict admission rejects absence into an ordinary bound error.
pn main() {
    var values: int[] = [7]
    var invalid: int = values[1]
    print([invalid is error, values[0], "continued"])
}
