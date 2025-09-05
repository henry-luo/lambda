// Test: Map/Dictionary Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Map literals
let empty_map = {}
let person = {
    name: "Alice",
    age: 30,
    active: true,
    scores: [95, 87, 92]
}

// Accessing values
let name = person.name      // "Alice"
let age = person["age"]     // 30
let missing = person.email  // undefined

// Modifying maps
person.city = "New York"
person["country"] = "USA"
person.age += 1  // 31

// Map methods
let keys = Object.keys(person)    // ["name", "age", "active", "scores", "city", "country"]
let values = Object.values(person) // ["Alice", 31, true, [95, 87, 92], "New York", "USA"]
let entries = Object.entries(person) // [["name", "Alice"], ["age", 31], ...]

// Checking existence
let hasName = "name" in person  // true
let hasEmail = "email" in person // false

// Nested maps
let company = {
    name: "Tech Corp",
    employees: [
        {name: "Alice", role: "Developer"},
        {name: "Bob", role: "Designer"}
    ],
    address: {
        street: "123 Tech St",
        city: "San Francisco"
    }
}

// Accessing nested properties
let first_employee = company.employees[0].name  // "Alice"
let city = company.address.city  // "San Francisco"

// Final check
1
