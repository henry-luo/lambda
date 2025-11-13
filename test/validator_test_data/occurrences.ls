// Lambda Validator Test Data: Occurrence Operators
// Tests ?, +, * occurrence operators for optionality and cardinality

// ========== Type Definitions ==========

// Optional (?) - zero or one
type OptionalString = string?
type OptionalInt = int?

// One or more (+) - at least one
type NonEmptyArray = [int]+
type NonEmptyString = string+

// Zero or more (*) - any number
type AnyArray = [string]*
type AnyInt = int*

// Complex occurrences on fields
type PersonWithOptionals = {
    name: string,           // required
    age: int?,              // optional
    emails: [string]+,      // at least one email
    phones: [string]*,      // any number of phones
    nickname: string?       // optional
}

// Occurrence on array element types
type FlexibleList = [int?]*    // Array of optional ints, any length
type RequiredList = [int]+     // At least one int

// ========== Valid Test Cases ==========

// Optional - present
let valid_optional_string: OptionalString = "hello"
let valid_optional_int: OptionalInt = 42

// Optional - absent (null)
let valid_optional_null: OptionalString = null
let valid_optional_int_null: OptionalInt = null

// One or more (+)
let valid_nonempty_array: NonEmptyArray = [1, 2, 3]
let valid_single_element: NonEmptyArray = [42]

// Zero or more (*)
let valid_any_array_full: AnyArray = ["a", "b", "c"]
let valid_any_array_empty: AnyArray = []

// Complex object with occurrences
let valid_person_full: PersonWithOptionals = {
    name: "Alice",
    age: 30,
    emails: ["alice@example.com", "alice@work.com"],
    phones: ["555-1234", "555-5678"],
    nickname: "Al"
}

let valid_person_minimal: PersonWithOptionals = {
    name: "Bob",
    emails: ["bob@example.com"],
    phones: []
}

// Flexible lists
let valid_flexible_full: FlexibleList = [1, null, 3, null, 5]
let valid_flexible_empty: FlexibleList = []

let valid_required_list: RequiredList = [1, 2, 3]

// ========== Invalid Test Cases ==========

// Optional present but wrong type
let invalid_optional_type: OptionalString = 42  // Error: Expected string or null, got int

// One or more (+) with zero elements
let invalid_empty_nonempty: NonEmptyArray = []  // Error: Expected at least 1 element, got 0

// Wrong base type in occurrence
let invalid_any_wrong_type: AnyArray = [1, 2, 3]  // Error: Expected [string]*, got [int]

// Missing required field (non-optional)
let invalid_person_missing_required: PersonWithOptionals = {
    age: 25,
    emails: ["test@example.com"],
    phones: []
    // Error: Missing required field 'name'
}

// Empty array for + occurrence
let invalid_person_empty_emails: PersonWithOptionals = {
    name: "Charlie",
    emails: [],  // Error: Expected at least 1 element, got 0
    phones: []
}

// Required list empty
let invalid_required_empty: RequiredList = []  // Error: Expected at least 1 element, got 0

// Occurrence mismatch - providing array when single expected
let invalid_single_as_array: OptionalInt = [1, 2, 3]  // Error: Expected int?, got [int]

// ========== Edge Cases ==========

// Multiple nulls in optional array
let edge_multiple_nulls: FlexibleList = [null, null, null]  // Valid: all optional ints (null)

// Single null vs empty array distinction
let edge_optional_vs_empty: PersonWithOptionals = {
    name: "Diana",
    age: null,      // Valid: optional int can be null
    emails: ["d@example.com"],
    phones: []      // Valid: * allows empty array
}
