// Deliberate syntax errors for robustness testing
// The fuzzer should handle these gracefully without crashing

// Unclosed delimiters
// [1, 2, 3
// {a: 1, b: 2
// (1 + 2
// "unclosed string

// Mismatched delimiters
// [1, 2, 3}
// {a: 1, b: 2]
// (1 + 2]

// Invalid operators
// 1 ++ 2
// 3 ** 4

// Invalid keywords
// lett x = 5
// func f() => 1

// Invalid literals
// 123abc
// 0x
// "unclosed

// Invalid function definitions
// fn () => 1
// fn f( => 1
// fn f) => 1

// Invalid expressions
// = 5
// let =
// let x = = 5

// Note: These are commented out as they would cause parse errors
// The actual fuzzer will generate these dynamically through mutations
let valid_placeholder = 1
