// T22-1b/c: an open operand against a short String literal emits exact inline
// equality and uses context-owned constants without a helper call.
pn literal_match_tune22(key) int {
    if (key == "level") { return 1 }
    if (key == "service") { return 2 }
    if (key == "status") { return 3 }
    return 0
}

pn main() {
    print(literal_match_tune22("service")); print(" ")
    print(literal_match_tune22("service-x")); print("\n")
}
