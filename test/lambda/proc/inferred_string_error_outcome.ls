// S7.7.4v2/S1.6: inferred string receivers retain errors and execute the tail.
pn check(source: string) {
    var line = "prefix"
    let snapshot = line
    // conversions retain errors; query input rejection now yields null
    line = line ++ int(source)
    let failed = line is error
    let message = line ^ { "recovered" }
    line = message ++ "!"
    fn captured() string^ => line
    line = line ++ " tail"
    return [failed, message, snapshot, line, captured()]
}

pn main() {
    var checks = []
    for (i in 1 to 12) {
        push(checks, check("hello"))
    }
    print(checks[0]); print("\n")
    print([len(checks), checks[0] == checks[11], "continued"]); print("\n")
}
