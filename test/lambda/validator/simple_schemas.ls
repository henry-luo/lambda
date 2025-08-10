// Simple schema definitions for testing
// Author: Henry Luo

// Basic primitive types
type StringType = string
type IntType = int  
type BoolType = bool
type FloatType = float

// Optional types
type OptionalString = string?
type OptionalInt = int?

// Array types
type StringArray = [string*]
type IntList = [int+]
type OptionalArray = [string?]*

// Simple map types
type PersonMap = {
    name: string,
    age: int
}

type OptionalFieldMap = {
    required_field: string,
    optional_field: int?
}

// Simple union types
type StringOrInt = string | int
type OptionalStringOrInt = (string | int)?

// Element types (for markup)
type SimpleElement = <div>
type ElementWithContent = <p string>
type ElementWithAttributes = <a href: string>

// Reference types
type PersonRef = PersonMap
type NestedRef = {
    person: PersonRef,
    id: int
}
