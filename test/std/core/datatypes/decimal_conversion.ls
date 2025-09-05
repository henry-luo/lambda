// Test: Decimal Type Conversion
// Category: core/datatypes
// Type: positive
// Expected: 1

// String to decimal conversion
let from_string = "123.456" as decimal
let from_int = 42 as decimal
let from_float = 3.14 as decimal  // Exact conversion

// Decimal to other types
let price = 19.99m
let price_str = price as string  // "19.99"
let price_float = price as float  // 19.99
let price_int = price as int     // 19 (truncates)

// Test parsing different formats
let scientific = "1.23e-4" as decimal  // If supported
let with_commas = "1,234.56" as decimal  // If supported

// Edge cases
let large_decimal = "79228162514264337593543950335" as decimal  // Max decimal
let small_decimal = "0.0000000000000000000000000001" as decimal  // Min decimal

// Final check
1
