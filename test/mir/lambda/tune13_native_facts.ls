// Tune13 P1: scalar result facts from audited numeric builtins survive typed
// local and return boundaries without an unnecessary Item check round-trip.

pn tune13_native_fact(values: int[]) int {
    var absolute: int = abs(values[0])
    var smaller: int = min(absolute, values[1])
    return smaller
}

pn main() {
    print(tune13_native_fact([-4, 3]))
    print("\n")
}
