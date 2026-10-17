// A rejected error must leave a `var` root untouched before COW preparation.
pn source_fail() int^ {
    raise error("source")
}

pn mutate(var value) {
    value = 999
    return 1
}

pn main() {
    var poisoned: any | error = null
    poisoned = source_fail()
    let outcome = mutate(poisoned)
    print((outcome ^ { 50 }) ++ "\n")
    print((poisoned ^ { 60 }) ++ "\n")
}
