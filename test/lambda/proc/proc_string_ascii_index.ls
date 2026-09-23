pn main() {
    var ascii: string = "abc"
    var unicode: string = "aé"
    print(ascii[1] == "b")
    print(" ")
    print(ascii[9] == null)   // S7.2.1: out of range is absence
    print(" ")
    print(unicode[1] == "é")
    print("\n")
}
