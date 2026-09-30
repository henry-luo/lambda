// Generic sequence and map tests use value kinds; a first-class type value
// still follows the ordinary `is` rule (S11.1.2v2, S2.5.1v2).
pn classify(value) {
    return [value is map, value is array]
}

pn dynamic(value, wanted) bool {
    return value is wanted
}

pn error_kind() {
    return [1 is error, error("x") is error]
}

pn main() {
    print([classify({a: 1}), classify([1]), classify(0 to 3),
        classify(null), dynamic({a: 1}, map), error_kind()])
}
