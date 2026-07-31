// A rejected error must leave a `var` root untouched before COW preparation.
pn source_fail() int^ {
    raise error("source")
}

pn mutate(var value) {
    value = 999
    return 1
}

pn main() {
    var poisoned = null
    poisoned = source_fail()
    print((mutate(poisoned) or 50) ++ "\n")
    print((poisoned or 60) ++ "\n")
}
