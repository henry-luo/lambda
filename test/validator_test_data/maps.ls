// Lambda Validator Test Data: Maps and Objects
// Tests map type validation with field types and requirements

// ========== Type Definitions ==========

type Person = {
    name: string,
    age: int,
    email: string
}

type OptionalFields = {
    required: string,
    optional?: int,
    nullable: int?
}

type NestedMap = {
    id: int,
    user: {
        name: string,
        address: {
            street: string,
            city: string,
            zip: int
        }
    }
}

type EmptyMap = {}

// ========== Valid Test Cases ==========

// Simple maps
let valid_person: Person = {
    name: "Alice",
    age: 30,
    email: "alice@example.com"
}

// Optional fields - all present
let valid_optional_all: OptionalFields = {
    required: "present",
    optional: 42,
    nullable: null
}

// Optional fields - some missing
let valid_optional_missing: OptionalFields = {
    required: "present",
    nullable: null
}

// Nested maps
let valid_nested: NestedMap = {
    id: 123,
    user: {
        name: "Bob",
        address: {
            street: "123 Main St",
            city: "Springfield",
            zip: 12345
        }
    }
}

// Empty map
let valid_empty: EmptyMap = {}

// ========== Invalid Test Cases ==========

// Missing required field
let invalid_missing_field: Person = {
    name: "Charlie",
    age: 25
    // Error: Missing required field 'email'
}

// Wrong field type
let invalid_field_type: Person = {
    name: "Diana",
    age: "thirty",  // Error: Expected int, got string
    email: "diana@example.com"
}

// Extra unexpected fields (if strict mode)
let invalid_extra_field: Person = {
    name: "Eve",
    age: 28,
    email: "eve@example.com",
    phone: "555-1234"  // Error: Unexpected field 'phone'
}

// Nested field type mismatch
let invalid_nested_type: NestedMap = {
    id: 456,
    user: {
        name: "Frank",
        address: {
            street: "456 Oak Ave",
            city: "Portland",
            zip: "97201"  // Error: Expected int, got string
        }
    }
}

// Missing nested required field
let invalid_nested_missing: NestedMap = {
    id: 789,
    user: {
        name: "Grace"
        // Error: Missing required field 'address'
    }
}

// Wrong container type
let invalid_map_not_map: Person = "not a map"  // Error: Expected map, got string
let invalid_map_array: Person = ["Alice", 30, "alice@example.com"]  // Error: Expected map, got array
