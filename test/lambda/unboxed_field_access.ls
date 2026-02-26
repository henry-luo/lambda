// Test unboxed context reads: when accessing typed map/object fields in
// arithmetic or other native-type contexts, skip redundant box/unbox round-trips.

// ===== INT field arithmetic =====
type Point = {x: int, y: int}
let p: Point = {x: 3, y: 4}

// native int arithmetic (no fn_add/fn_mul boxing)
p.x + p.y
p.x * p.y
p.x - p.y

// multi-operand native arithmetic
p.x * p.x + p.y * p.y

// ===== FLOAT field arithmetic =====
type Vec2 = {x: float, y: float}
let v: Vec2 = {x: 3.5, y: 4.5}

// native float arithmetic (no push_d heap alloc per read)
v.x + v.y
v.x * v.y
v.x - v.y

// ===== BOOL fields =====
type Flags = {a: bool, b: bool}
let f: Flags = {a: true, b: false}
[f.a, f.b]

// ===== STRING fields =====
type Name = {first: string, last: string}
let n: Name = {first: "Alice", last: "Smith"}
[n.first, n.last]

// ===== Mixed type map — boxing preserved per field =====
type Person = {name: string, age: int, active: bool}
let alice: Person = {name: "Alice", age: 30, active: true}
[alice.name, alice.age + 5, alice.active]

// ===== Fields in list context (box on demand) =====
[p.x, p.y]
[alice.name, alice.age, alice.active]

// ===== Function with typed params =====
fn dot_product(a: Point, b: Point) { a.x * b.x + a.y * b.y }
dot_product({x: 2, y: 3}, {x: 4, y: 5})
