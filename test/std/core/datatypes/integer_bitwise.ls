// Test: Integer Bitwise Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test bitwise operations
let a = 0b1100  // 12
let b = 0b1010  // 10

// Bitwise AND
let and = a & b  // 0b1000 (8)

// Bitwise OR
let or = a | b   // 0b1110 (14)

// Bitwise XOR
let xor = a ^ b   // 0b0110 (6)

// Bitwise NOT
let not_a = ~a    // Depends on integer size

// Left shift
let left_shift = a << 2  // 0b110000 (48)

// Right shift
let right_shift = a >> 1  // 0b0110 (6)

// Unsigned right shift (logical shift)
let unsigned_right = a >>> 1  // 0b0110 (6) for positive numbers

// Test with negative numbers
let neg = -1
let neg_shift = neg >> 1  // Arithmetic shift (sign-extended)
let neg_ushift = neg >>> 1 // Logical shift (zero-extended)

// Final check
1
