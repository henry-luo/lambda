// More comprehensive schema for testing validation
// Author: Henry Luo

// Strict person type for testing validation
type StrictPerson = {
    name: string,           // Required field
    age: int,              // Must be integer, not string
    email: string?         // Optional field
}

// Document type that requires all fields
type TestDocument = {
    person: StrictPerson,
    valid: bool
}
