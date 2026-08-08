// Tune15 B4: an exclusive mutable string binding grows in place.
pn main() {
    var s: string = ""
    var i: int = 0
    while (i < 3) {
        s = s ++ "x"
        i = i + 1
    }
    print(s)
    print("\n")
}
