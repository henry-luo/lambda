// Test: Data Transformation
// Layer: 1 | Category: integration | Covers: map, filter, transform, chain

let data = [
    {name: "Alice", age: 30, dept: "eng"},
    {name: "Bob", age: 25, dept: "sales"},
    {name: "Carol", age: 35, dept: "eng"},
    {name: "Dave", age: 28, dept: "sales"},
    {name: "Eve", age: 32, dept: "eng"}
]

// Extract names
data | ~.name

// Filter by age
data that (~.age >= 30) | ~.name

// Count
len(data)
len(data that (~.dept == "eng"))

// Average age
avg(data | ~.age)

// Transform to new structure
data | {person: ~.name, years: ~.age}

// Sum ages
sum(data | ~.age)

// Youngest and oldest
min(data | ~.age)
max(data | ~.age)
