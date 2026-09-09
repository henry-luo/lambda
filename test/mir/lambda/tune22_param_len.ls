// T22-2b: a typed immutable array parameter has one admitted ArrayNum carrier.
// Its entry cache supplies a loop-bound length without moving any user
// expression out of a potentially zero-trip loop.
pn tune22_param_len(values: int[]) int {
    var i: int = 0
    while (i < len(values)) {
        i = i + 1
    }
    return i
}

pn main() {
    print(tune22_param_len([4, 5, 6])); print(" ")
    print(tune22_param_len([])); print("\n")
}
