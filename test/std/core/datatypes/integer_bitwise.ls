// Test: Integer Bitwise Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test bitwise operations using decimal numbers
// Original binary: 0b1100 = 12, 0b1010 = 10
let a = 12  // 0b1100
a
let b = 10  // 0b1010
b

// Bitwise AND (should be 0b1000 = 8)
let and = a & b  
and

// Bitwise OR (should be 0b1110 = 14)
let or = a | b   
or

// Bitwise XOR (should be 0b0110 = 6)
let xor = a ^ b   
xor

// Bitwise NOT (depends on integer size, typically -13 for 32-bit)
let not_a = ~a    
not_a

// Left shift (should be 0b110000 = 48)
let left_shift = a << 2  
left_shift

// Right shift (should be 0b0011 = 3)
let right_shift = a >> 1  
right_shift

// Test with negative numbers
let neg = -1
neg

// Right shift of negative (sign-extending)
let neg_shift = neg >> 1  
neg_shift

1
