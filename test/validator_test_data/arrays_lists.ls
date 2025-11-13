// Lambda Validator Test Data: Arrays and Lists
// Tests array/list type validation with nested types

// ========== Type Definitions ==========

type IntArray = [int]
type StringArray = [string]
type MixedList = (string, int, bool)
type NestedArray = [[int]]
type ArrayOfMaps = [{name: string, age: int}]

// ========== Valid Test Cases ==========

// Simple arrays
let valid_int_array: IntArray = [1, 2, 3, 4, 5]
let empty_int_array: IntArray = []
let single_int_array: IntArray = [42]

let valid_string_array: StringArray = ["hello", "world", "test"]

// Lists (tuples)
let valid_mixed_list: MixedList = ("Alice", 30, true)

// Nested arrays
let valid_nested: NestedArray = [[1, 2], [3, 4], [5]]
let empty_nested: NestedArray = []

// Arrays of maps
let valid_array_maps: ArrayOfMaps = [
    {name: "Alice", age: 30},
    {name: "Bob", age: 25},
    {name: "Carol", age: 35}
]

// ========== Invalid Test Cases ==========

// Type mismatches in arrays
let invalid_int_array_string: IntArray = ["not", "ints"]
let invalid_mixed_array: IntArray = [1, "two", 3]  // Error: element 1 is string, not int

// Wrong array element types
let invalid_nested_flat: NestedArray = [1, 2, 3]  // Error: expected [[int]], got [int]

// List arity mismatch
let invalid_list_short: MixedList = ("Alice", 30)  // Error: expected 3 elements, got 2
let invalid_list_long: MixedList = ("Alice", 30, true, "extra")  // Error: too many elements

// List element type mismatch
let invalid_list_types: MixedList = (30, "Alice", true)  // Error: element 0 should be string

// Wrong container type
let invalid_array_not_array: IntArray = {a: 1, b: 2}  // Error: expected array, got map
