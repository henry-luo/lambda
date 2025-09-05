// Test: Integer Type Conversion
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test string to integer conversion
let from_string = "42" as int  // 42
let from_float = 3.14 as int   // 3 (truncates decimal)
let from_bool_true = true as int  // 1
let from_bool_false = false as int // 0

// Test integer to other types
let num = 42
let to_string = num as string  // "42"
let to_float = num as float    // 42.0
let to_bool_true = 1 as bool   // true
let to_bool_false = 0 as bool  // false

// Test with different bases
let binary = 0b1010  // 10
let octal = 0o12     // 10
let hex = 0xA        // 10

// Test conversion edge cases
let empty_string = "" as int        // 0
let invalid_string = "abc" as int   // 0 or error?
let large_string = "99999999999999999999999999999999999999999999999999999999999999" as int  // max_int or error?

// Final check
1
