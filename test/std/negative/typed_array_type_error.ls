// Test: Typed Array Type Error
// Layer: 2 | Category: negative | Covers: assign wrong type to typed array

// Assign string to int array - should produce error
let arr: int[] = [1, 2, "hello"]

// Mixed types in typed float array
let farr: float[] = [1.0, "bad", 3.0]
