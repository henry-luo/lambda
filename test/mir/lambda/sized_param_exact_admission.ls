// D2.4.1-D2.4.3: an already canonical packed sized int can cross its parameter
// boundary unchanged; a different source type keeps the coercion fallback.
pn accept_u32(x: u32) int { return int(x) }
pn accept_i8(x: i8) int { return int(x) }
pn accept_u16(x: u16) int { return int(x) }

pn main() {
    print(accept_u32(7u32)); print(" ")
    print(accept_u32(7)); print(" ")
    print(accept_i8(-1i8)); print(" ")
    print(accept_u16(65535u16)); print(" ")
    print(accept_i8(7)); print(" ")
    print(accept_u16(7)); print(" ")
    print(int(shl(1u32, 1))); print(" ")
    print(int(shl(1u32, -1))); print(" ")
    print(bor(shl(1u32, -1), 1u32)); print(" ")
    print(int(bor(shl(1u32, -1), 1u32))); print(" ")
    print(shl(1u32, -1) + 1u32); print(" ")
    var maybe: int? = null
    print(ushr(maybe, 1u32)); print(" ")
    print(shl(1u32, maybe)); print("\n")
}
