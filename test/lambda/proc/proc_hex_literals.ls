// Test hexadecimal literals in procedural variables.

pn main() {
    var mask = 0xFFi8
    print("mask:")
    print(mask)
    print(":")
    print(type(mask))

    var wide: u32 = 0xFFFFFFFFu32
    print(" wide:")
    print(wide)
    print(":")
    print(type(wide))
}
