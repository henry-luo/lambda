// Tune15 B1.2: proved scalar conversions stay in their native carriers.
pn tune15_int_from_float(value: float) int {
    return int(value)
}

pn tune15_float_from_int(value: int) float {
    return float(value)
}

pn tune15_trunc(value: float) float {
    return trunc(value)
}

pn tune15_ord(value: string) {
    print(string(ord(value)) ++ "\n")
}

pn main() {
    print(string(tune15_int_from_float(4.75)) ++ ",")
    print(string(tune15_float_from_int(7)) ++ ",")
    print(string(tune15_trunc(4.75)) ++ "\n")
    tune15_ord("A")
}
