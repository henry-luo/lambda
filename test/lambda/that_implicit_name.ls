// Test: 'that' clause with implicit ~.name resolution
// In 'that' clause, bare identifiers not in scope resolve to ~.name
// Name resolution order: 1) scope names, 2) ~.name, 3) system properties

// ============================================================
// Section 1: Basic expr-level 'that' with map items
// ============================================================

'=1a='
// Filter maps by field - traditional syntax with ~.name
[{name: "alice", age: 30}, {name: "bob", age: 25}] that (~.age > 28)

'=1b='
// Same filter using implicit name resolution (age instead of ~.age)
[{name: "alice", age: 30}, {name: "bob", age: 25}] that (age > 28)

'=1c='
// Access multiple fields implicitly
[{name: "alice", age: 30}, {name: "bob", age: 25}] that (age >= 25 and name == "bob")

// ============================================================
// Section 2: Scope names take priority over field names
// ============================================================

'=2a='
// let binding 'age' in scope should win over ~.age
let age = 25
[{name: "alice", age: 30}, {name: "bob", age: 20}] that (age > 28)

'=2b='
// After the let, 'age' refers to the scope variable (25), not the field
// So 25 > 28 is false for all items
let age2 = 25
[{name: "alice", age: 30}, {name: "bob", age: 20}] that (age2 > 28)

// ============================================================
// Section 3: Pipe transform with 'that' uses implicit name 
// ============================================================

'=3a='
// Pipe with that: filter using implicit field names
[{x: 1, y: 2}, {x: 3, y: 4}] that (x > 2)

'=3b='
// Pipe with that: combined field check
[{x: 10, y: 20}, {x: 30, y: 40}] that (x + y > 50)

// ============================================================
// Section 4: Object-level that-constraint with implicit name
// (Tested separately in map_object_robustness.ls)
// ============================================================

// ============================================================
// Section 5: Mix of ~ and implicit names
// ============================================================

'=5a='
// Can still use ~ explicitly alongside implicit names
[{name: "alice", age: 30}, {name: "bob", age: 25}] that (name == "alice")

'=5b='
// Nested field access: 'that' only applies implicit ~ to first level
[{a: 1, b: 2}, {a: 3, b: 4}] that (a > 2)

