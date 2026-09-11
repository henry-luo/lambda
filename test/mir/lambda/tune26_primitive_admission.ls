// Tune26 T26-1: matching owned primitive ArrayNum carriers establish their
// declared contracts without an element-by-element admission pass.

pn tune26_bool_flags(n: int) int {
    var flags: bool[] = fill(n, true)
    flags[1] = false
    var count = 0
    if (flags[0]) { count = count + 1 }
    if (flags[1]) { count = count + 1 }
    if (flags[n - 1]) { count = count + 1 }
    return count
}

pn tune26_numeric_fill() float {
    var ints: int[] = fill(3, 7)
    var floats: float[] = fill(2, 1.5)
    return ints[1] + floats[0] + floats[1]
}

pn main() {
    print(tune26_bool_flags(4)); print(" ")
    print(tune26_numeric_fill()); print("\n")
}
