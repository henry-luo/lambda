// Test bitwise operations with INT64 operands
// len() returns INT64 — this tests that shr/shl/band/bor/bxor/bnot
// correctly handle INT64 values without incorrectly unboxing them.
// Regression test for MIR transpiler bug: bitwise inline emitters
// called it2i() on raw native int64 values, treating them as tagged Items.

pn main() {
    // len() returns INT64 — use it as operand for all bitwise ops
    let s = "abcdefgh"       // len = 8
    let n = len(s)           // INT64 value 8

    // shr with INT64 first operand
    print("shr(len,1)=")
    print(shr(n, 1))         // 8 >> 1 = 4
    print("\n")

    // shr with INT64 second operand (shift amount from len)
    let s2 = "ab"            // len = 2
    print("shr(256,len)=")
    print(shr(256, len(s2))) // 256 >> 2 = 64
    print("\n")

    // shr with both INT64
    print("shr(len8,len2)=")
    print(shr(n, len(s2)))   // 8 >> 2 = 2
    print("\n")

    // shl with INT64 first operand
    print("shl(len,2)=")
    print(shl(n, 2))         // 8 << 2 = 32
    print("\n")

    // shl with INT64 second operand
    print("shl(1,len)=")
    print(shl(1, n))         // 1 << 8 = 256
    print("\n")

    // band with INT64 operands
    print("band(len,3)=")
    print(band(n, 3))        // 8 & 3 = 0 (1000 & 0011)
    print("\n")

    print("band(15,len)=")
    print(band(15, n))       // 15 & 8 = 8 (1111 & 1000)
    print("\n")

    // bor with INT64 operands
    print("bor(len,3)=")
    print(bor(n, 3))         // 8 | 3 = 11 (1000 | 0011)
    print("\n")

    print("bor(5,len)=")
    print(bor(5, n))         // 5 | 8 = 13 (0101 | 1000)
    print("\n")

    // bxor with INT64 operands
    print("bxor(len,15)=")
    print(bxor(n, 15))       // 8 ^ 15 = 7 (1000 ^ 1111)
    print("\n")

    print("bxor(255,len)=")
    print(bxor(255, n))      // 255 ^ 8 = 247
    print("\n")

    // bnot with INT64 operand
    print("bnot(len)=")
    print(bnot(n))           // ~8 = -9
    print("\n")

    // Practical use case: shr(len(str), 2) for base64 decode length
    let encoded = "YWJjZGVmZ2g="  // base64 of "abcdefgh", len=12
    let slen = len(encoded)
    print("b64_blocks=")
    print(shr(slen, 2))      // 12 >> 2 = 3
    print("\n")

    // Combined: bitwise ops with INT64 in expressions
    let arr = fill(16, 0)
    let idx = len(arr)       // 16
    print("idx_lo=")
    print(band(idx, 15))     // 16 & 15 = 0
    print("\n")

    print("idx_hi=")
    print(shr(idx, 4))       // 16 >> 4 = 1
    print("\n")
}
