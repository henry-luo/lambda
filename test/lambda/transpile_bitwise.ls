// Test bitwise operations: band, bor, bxor, bnot, shl, shr
// Verifies native int64_t argument dispatch (C_ARG_NATIVE) from Phase 4

// ============================================
// Section 1: Basic bitwise operations
// ============================================

"1. Basic bitwise ops"
[
    band(255, 15),       // 15
    bor(240, 15),        // 255
    bxor(255, 15),       // 240
    bnot(0),                 // -1
    shl(1, 4),              // 16
    shr(256, 4)             // 16
]

// ============================================
// Section 2: Bitwise ops inside typed functions
// ============================================

fn typed_band(a: int, b: int) { band(a, b) }
fn typed_bor(a: int, b: int) { bor(a, b) }
fn typed_bxor(a: int, b: int) { bxor(a, b) }
fn typed_bnot(a: int) { bnot(a) }
fn typed_shl(a: int, b: int) { shl(a, b) }
fn typed_shr(a: int, b: int) { shr(a, b) }

"2. Typed function bitwise ops"
[
    typed_band(255, 15),
    typed_bor(240, 15),
    typed_bxor(255, 15),
    typed_bnot(0),
    typed_shl(1, 4),
    typed_shr(256, 4)
]

// ============================================
// Section 3: Bitwise with computed values
// ============================================

fn mask_low_bits(val: int, bits: int) {
    let mask = shl(1, bits) - 1
    band(val, mask)
}

fn set_bit(val: int, bit: int) {
    bor(val, shl(1, bit))
}

fn clear_bit(val: int, bit: int) {
    band(val, bnot(shl(1, bit)))
}

fn test_bit(val: int, bit: int) {
    band(val, shl(1, bit)) != 0
}

"3. Computed bitwise"
[
    mask_low_bits(255, 4),     // 15
    set_bit(0, 3),             // 8
    clear_bit(15, 1),          // 13
    test_bit(8, 3),            // true
    test_bit(8, 2)             // false
]

// ============================================
// Section 4: Bitwise with untyped args
// ============================================

fn untyped_band(a, b) { band(a, b) }
fn untyped_bor(a, b) { bor(a, b) }

"4. Untyped bitwise"
[
    untyped_band(255, 15),
    untyped_bor(240, 15)
]

// ============================================
// Section 5: Chained bitwise
// ============================================

fn encode_rgb(r: int, g: int, b: int) {
    bor(bor(shl(r, 16), shl(g, 8)), b)
}

fn decode_r(rgb: int) { band(shr(rgb, 16), 255) }
fn decode_g(rgb: int) { band(shr(rgb, 8), 255) }
fn decode_b(rgb: int) { band(rgb, 255) }

"5. RGB encode/decode"
let color = encode_rgb(255, 128, 64)
[color, decode_r(color), decode_g(color), decode_b(color)]

// ============================================
// Section 6: Bitwise in list context (boxing result)
// ============================================

"6. Bitwise results boxed in list"
[band(7, 3), bor(4, 2), bxor(5, 3), bnot(-1), shl(3, 2), shr(12, 2)]

// ============================================
// Section 7: Edge cases
// ============================================

"7. Edge cases"
[
    band(0, 0),         // 0
    bor(0, 0),          // 0
    bxor(0, 0),         // 0
    bnot(-1),           // 0
    shl(0, 10),         // 0
    shr(0, 10)          // 0
]
