// Test direct field access optimization for typed maps
// Type-defined maps use compile-time byte offsets for reads/writes

// typed map with int fields
type Point = {x: int, y: int}

// typed map with mixed field types
type Person = {name: string, age: int, active: bool}

// Test 1: basic field read
let p: Point = {x: 10, y: 20}
[p.x, p.y]

// Test 2: field arithmetic
let q: Point = {x: 3, y: 4}
q.x * q.x + q.y * q.y

// Test 3: mixed types
let alice: Person = {name: "Alice", age: 30, active: true}
[alice.name, alice.age, alice.active]

// Test 4: multiple instances share the same type
let bob: Person = {name: "Bob", age: 25, active: false}
[bob.name, bob.age, bob.active]

// Test 5: pass typed map to function
fn distance(a: Point, b: Point) {
    let dx = a.x - b.x
    let dy = a.y - b.y
    math.sqrt(dx * dx + dy * dy)
}
distance({x: 0, y: 0}, {x: 3, y: 4})
