// Tune16 C5: loop-carried string concatenation flattens a nested RHS join
// into the same owned builder buffer.

pn main() {
    var result: string = ""
    var i: int = 0
    while (i < 2) {
        var a: string = "a"
        var b: string = "b"
        var c: string = "c"
        var d: string = "d"
        result = result ++ (a ++ b ++ c ++ d)
        i = i + 1
    }
    print(result)
    print("\n")
}
