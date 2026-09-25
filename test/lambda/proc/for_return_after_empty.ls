// A return inside a for body cannot hide the return on the zero-iteration path.
pn first_or_empty(values) string {
    for (value in values) { return "hit" }
    return "empty"
}

pn main() {
    print(first_or_empty([]) ++ "\n")
    print(first_or_empty([1]) ++ "\n")
}
