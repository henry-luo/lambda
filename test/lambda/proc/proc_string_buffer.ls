// Regression coverage for Tune10's exclusive String buffer rebinding.
pn main() {
    var value = "a"
    let first_alias = value
    value = value ++ "b"
    print("T1:" ++ first_alias ++ ":" ++ value)

    let frozen_alias = value
    value = value ++ "c"
    print(" T2:" ++ frozen_alias ++ ":" ++ value)

    var utf8 = "é"
    utf8 = utf8 ++ "x"
    print(" T3:" ++ utf8)

    var repeated = ""
    var i = 0
    while (i < 128) {
        repeated = repeated ++ "x"
        i = i + 1
    }
    print(" T4:" ++ len(repeated))
}
