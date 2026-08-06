// Tune13 P1: an exact, non-widened int[] binding may cross an identical
// occurrence boundary without re-entering the generic type checker.

pn tune13_first(values: int[]) int {
    return values[0]
}

pn tune13_stable_call(values: int[]) int {
    return tune13_first(values)
}

pn tune13_widened_call(values: any[]) int {
    return tune13_first(values)
}

pn main() {
    print(tune13_stable_call(fill(2, 7)))
    print("\n")
    print(tune13_widened_call(fill(2, 7)))
    print("\n")
}
