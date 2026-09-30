// S4.4.4: a sized shift at or beyond its width yields zero on both tiers.
// The generated zero arm must not fall back into the host-width shift.
pn left(x: u32, n: int) u32 { return shl(x, n) }
pn right(x: u32, n: int) u32 { return shr(x, n) }

pn main() {
    print(left(1u32, 31)); print(" ")
    print(left(1u32, 32)); print(" ")
    print(left(1u32, 33)); print(" ")
    print(left(1u32, 64)); print("\n")
    print(right(4294967295u32, 31)); print(" ")
    print(right(4294967295u32, 32)); print(" ")
    print(right(4294967295u32, 33)); print(" ")
    print(right(4294967295u32, 64)); print("\n")
}
