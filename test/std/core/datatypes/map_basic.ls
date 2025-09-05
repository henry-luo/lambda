// Test: Map/Dictionary Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Map literals
let empty_map = {}
empty_map
let person = {
person
    name: "Alice",
    age: 30,
    active: true,
    scores: [95, 87, 92]
}
let name = person.name      
name
let age = person["age"]     
age
let missing = person.email  
missing
person.city = "New York"
person["country"] = "USA"
person.age += 1  // 31
let keys = Object.keys(person)    
keys
let values = Object.values(person) 
values
let entries = Object.entries(person) 
entries
let hasName = "name" in person  
hasName
let hasEmail = "email" in person 
hasEmail
let company = {
company
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
let first_employee = company.employees[0].name  
first_employee
let city = company.address.city  
city
1
