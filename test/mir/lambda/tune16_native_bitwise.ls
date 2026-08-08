// Tune16 C2: proved integer bitwise operations stay on native MIR lanes.

pn tune16_native_bitwise(a: int, b: int) int {
    var shared: int = band(a, b)
    var different: int = bxor(a, b)
    return bor(shared, different)
}

pn main() {
    print(tune16_native_bitwise(1024, 255))
    print("\n")
}
