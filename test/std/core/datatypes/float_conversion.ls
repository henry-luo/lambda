// Test: Floating-Point Type Conversion
// Category: core/datatypes
// Type: positive
// Expected: 1

// String to float conversion
let from_string = "3.14" as float
let from_int = 42 as float
let from_bool_true = true as float  // 1.0
let from_bool_false = false as float // 0.0

// Float to other types
let pi = 3.14159
let to_string = pi as string
let to_int = pi as int  // Truncates to 3
let to_bool1 = 1.0 as bool  // true
let to_bool0 = 0.0 as bool  // false

// Edge cases
let nan = 0.0 / 0.0
let inf = 1.0 / 0.0
let nan_str = nan as string
let inf_str = inf as string

// Test parsing different formats
let scientific = "1.23e-4" as float
let hex_float = "0x1.fffffep+127" as float  // If supported

// Final check
1
