// Test: Bitwise Operators
// Layer: 2 | Category: operator | Covers: band, bor, bxor, bnot, shl, shr

// ===== Bitwise AND =====
band(0xFF, 0x0F)
band(0xC, 0xA)
band(255, 128)

// ===== Bitwise OR =====
bor(0xF0, 0x0F)
bor(0xC, 0x3)

// ===== Bitwise XOR =====
bxor(0xFF, 0x0F)
bxor(0xC, 0xA)

// ===== Bitwise NOT =====
bnot(0)
bnot(0xFF)

// ===== Bit shift left =====
shl(1, 0)
shl(1, 1)
shl(1, 8)

// ===== Bit shift right =====
shr(256, 1)
shr(256, 8)
shr(0xFF, 4)

// ===== Combined operations =====
bor(shl(1, 3), shl(1, 0))
band(bor(0xF0, 0x0F), 0xFF)
