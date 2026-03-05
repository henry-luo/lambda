// Test: Bitwise Operators
// Layer: 2 | Category: operator | Covers: band, bor, bxor, bnot, bshl, bshr

// ===== Bitwise AND =====
band(0xFF, 0x0F)
band(0b1100, 0b1010)
band(255, 128)

// ===== Bitwise OR =====
bor(0xF0, 0x0F)
bor(0b1100, 0b0011)

// ===== Bitwise XOR =====
bxor(0xFF, 0x0F)
bxor(0b1100, 0b1010)

// ===== Bitwise NOT =====
bnot(0)
bnot(0xFF)

// ===== Bit shift left =====
bshl(1, 0)
bshl(1, 1)
bshl(1, 8)

// ===== Bit shift right =====
bshr(256, 1)
bshr(256, 8)
bshr(0xFF, 4)

// ===== Combined operations =====
bor(bshl(1, 3), bshl(1, 0))
band(bor(0xF0, 0x0F), 0xFF)
