// Lambda Validator Test Data: Union Types
// Tests union type validation (type1 | type2)

// ========== Type Definitions ==========

// Simple unions
type StringOrInt = string | int
type NumberOrBool = int | float | bool
type NullableString = string | null

// Union with containers
type ListOrMap = [int] | {count: int}
type ArrayOrSingle = [string] | string

// Complex unions
type Response = {success: bool, data: string} | {success: bool, error: string}

type Value = int | float | string | bool | null

type ID = int | string  // Can be numeric ID or UUID string

type Contact = {
    name: string,
    info: string | {email: string, phone: string}  // Union field
}

// Nested unions
type NestedUnion = (int | string) | (bool | null)  // Equivalent to int | string | bool | null

// ========== Valid Test Cases ==========

// Simple unions - first alternative
let valid_string_or_int_str: StringOrInt = "hello"
let valid_string_or_int_int: StringOrInt = 42

// Simple unions - various alternatives
let valid_number_int: NumberOrBool = 100
let valid_number_float: NumberOrBool = 3.14
let valid_number_bool: NumberOrBool = true

// Nullable string
let valid_nullable_string: NullableString = "text"
let valid_nullable_null: NullableString = null

// List or map - list alternative
let valid_list_or_map_list: ListOrMap = [1, 2, 3]
let valid_list_or_map_map: ListOrMap = {count: 5}

// Array or single
let valid_array_or_single_array: ArrayOrSingle = ["a", "b", "c"]
let valid_array_or_single_single: ArrayOrSingle = "just one"

// Complex response - success case
let valid_response_success: Response = {
    success: true,
    data: "Operation completed"
}

// Complex response - error case
let valid_response_error: Response = {
    success: false,
    error: "Something went wrong"
}

// Value union - various types
let valid_value_int: Value = 42
let valid_value_float: Value = 3.14
let valid_value_string: Value = "text"
let valid_value_bool: Value = false
let valid_value_null: Value = null

// ID as int or string
let valid_id_int: ID = 12345
let valid_id_string: ID = "uuid-1234-5678-90ab"

// Contact with string info
let valid_contact_simple: Contact = {
    name: "Alice",
    info: "alice@example.com"
}

// Contact with map info
let valid_contact_detailed: Contact = {
    name: "Bob",
    info: {
        email: "bob@example.com",
        phone: "555-1234"
    }
}

// Nested unions
let valid_nested_int: NestedUnion = 42
let valid_nested_string: NestedUnion = "hello"
let valid_nested_bool: NestedUnion = true
let valid_nested_null: NestedUnion = null

// ========== Invalid Test Cases ==========

// Type not in union
let invalid_string_or_int: StringOrInt = true  // Error: Expected string | int, got bool
let invalid_number: NumberOrBool = "text"  // Error: Expected int | float | bool, got string

// Nullable - wrong type when not null
let invalid_nullable: NullableString = 42  // Error: Expected string | null, got int

// Container union mismatch
let invalid_list_or_map: ListOrMap = "not list or map"  // Error: Expected [int] | {count: int}, got string

// Response with wrong fields
let invalid_response_mixed: Response = {
    success: true,
    data: "some data",
    error: "also error"  // Error: Cannot have both data and error
}

// Response missing required field
let invalid_response_incomplete: Response = {
    success: true
    // Error: Must have either 'data' or 'error' field
}

// Value union - completely wrong type
let invalid_value: Value = {key: "value"}  // Error: Expected int | float | string | bool | null, got map

// Array or single - wrong element type in array
let invalid_array_or_single: ArrayOrSingle = [1, 2, 3]  // Error: Expected [string] | string, got [int]

// Contact with wrong info type
let invalid_contact_number: Contact = {
    name: "Charlie",
    info: 12345  // Error: Expected string | {email, phone}, got int
}

// Contact with incomplete map
let invalid_contact_partial: Contact = {
    name: "Diana",
    info: {email: "diana@example.com"}  // Error: Map missing 'phone' field
}

// ========== Edge Cases ==========

// Ambiguous unions (both could match)
type AmbiguousUnion = {a: int} | {a: string}
let edge_ambiguous: AmbiguousUnion = {a: 42}  // Should match first alternative

// Empty containers in unions
type EmptyOrNot = [] | [int]
let edge_empty: EmptyOrNot = []  // Valid: matches empty array

// Union with overlapping types
type IntOrNumber = int | (int | float)  // Redundant but valid
let edge_overlapping: IntOrNumber = 42  // Valid: int
