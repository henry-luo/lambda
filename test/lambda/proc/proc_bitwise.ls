// Test bitwise operations
// P0 feature for AWFY benchmarks

pn main() {
    // band (AND)
    print(band(255, 15))
    print(" ")
    print(band(170, 85))
    print("\n")

    // bor (OR)
    print(bor(240, 15))
    print(" ")
    print(bor(170, 85))
    print("\n")

    // bxor (XOR)
    print(bxor(255, 15))
    print(" ")
    print(bxor(170, 85))
    print("\n")

    // bnot (NOT)
    print(bnot(0))
    print(" ")
    print(bnot(-1))
    print("\n")

    // shl (left shift)
    print(shl(1, 0))
    print(" ")
    print(shl(1, 4))
    print(" ")
    print(shl(1, 10))
    print("\n")

    // shr (right shift)
    print(shr(256, 4))
    print(" ")
    print(shr(1024, 10))
    print("\n")

    // Combined operations
    print(bor(band(255, 240), band(255, 15)))
    print(" ")
    print(bxor(band(255, 170), bor(0, 85)))
    print("\n")
}
