// Test: String Type Conversion and Formatting
// Category: core/datatypes
// Type: positive
// Expected: 1

// String to other types
let num_str = "42"
let num = num_str as int           // 42
let float_num = "3.14" as float    // 3.14
let bool_true = "true" as bool     // true
let bool_false = "false" as bool   // false

// Other types to string
let int_str = 42 as string         // "42"
let float_str = 3.14 as string     // "3.14"
let bool_str = true as string      // "true"
let null_str = null as string      // "null"

// String formatting
let name = "Alice"
let age = 30
let formatted = `Name: ${name}, Age: ${age}`  // "Name: Alice, Age: 30"

// Number formatting
let price = 19.99
let price_str = price.format("0.00")  // "19.99"

// Date formatting
let now = date()
let date_str = now.format("YYYY-MM-DD")  // e.g., "2023-05-15"

// Padding and alignment
let padded = "42".pad_start(5)     // "   42"
let right_aligned = "42".rjust(5)   // "   42"
let left_aligned = "42".ljust(5)    // "42   "
let centered = "42".center(6, "-")  // "--42--"

// Final check
1
