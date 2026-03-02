// Test Phase 7: Typed function parameter passing with Map* signatures
// When a function parameter has a typed map/object annotation, field access
// inside the function body should use direct struct access instead of map_get/fn_member.

// ---- Map-typed parameters (type Name = {…}) ----

// Test 1: basic float field reads on map params
type Point = {x: float, y: float}
fn distance(a: Point, b: Point) => math.sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y))
let p1: Point = {x: 3.0, y: 0.0}
let p2: Point = {x: 0.0, y: 4.0}
distance(p1, p2)

// Test 2: int field reads on map params
type Size = {w: int, h: int}
fn area(s: Size) => s.w * s.h
fn perimeter(s: Size) => 2 * (s.w + s.h)
let sz: Size = {w: 10, h: 5}
area(sz)
perimeter(sz)

// Test 3: mixed-type field reads
type Person = {name: string, age: int, active: bool}
fn greeting(p: Person) => "Hello, " ++ p.name
fn is_senior(p: Person) => p.age >= 65
let alice: Person = {name: "Alice", age: 30, active: true}
greeting(alice)
is_senior(alice)

// Test 4: two map params of same type
fn swap_names(a: Person, b: Person) => [b.name, a.name]
let bob: Person = {name: "Bob", age: 25, active: true}
swap_names(alice, bob)

// Test 5: map param with arithmetic across fields of same param
type Rect = {x: float, y: float, w: float, h: float}
fn center(r: Rect) => [r.x + r.w / 2.0, r.y + r.h / 2.0]
let r: Rect = {x: 10.0, y: 20.0, w: 100.0, h: 50.0}
center(r)

// Test 6: map param + scalar param
fn offset_area(s: Size, factor: int) => s.w * s.h * factor
offset_area(sz, 3)

// Test 7: map param used multiple times in expression
fn diagonal(s: Size) => math.sqrt(float(s.w * s.w + s.h * s.h))
diagonal(sz)

// Test 8: function returning a typed map, called with typed param
type Vec2 = {x: float, y: float}
fn length(v: Vec2) => math.sqrt(v.x * v.x + v.y * v.y)
let v: Vec2 = {x: 3.0, y: 4.0}
length(v)

// ---- Object-typed parameters ----

// Test 9: object param with fn method
type Counter {
    val: int = 0;
    fn doubled() => val * 2
    pn inc() { val = val + 1 }
}
fn read_counter(c: Counter) => c.val
let cnt = {Counter}
cnt.inc()
cnt.inc()
cnt.inc()
read_counter(cnt)

// Test 10: object param accessing multiple fields
type Pair {
    a: int,
    b: int;
    fn sum() => a + b
}
fn diff_pair(p: Pair) => p.a - p.b
let pr = {Pair a: 10, b: 3}
diff_pair(pr)

// Test 11: two object params of different types
type NamedVal {
    label: string,
    num: int;
}
fn format_nv(nv: NamedVal) => nv.label ++ "=" ++ string(nv.num)
let nv1 = {NamedVal label: "score", num: 42}
format_nv(nv1)

// Test 12: nested function calls with typed params
fn add_x(a: Point, b: Point) => a.x + b.x
fn add_y(a: Point, b: Point) => a.y + b.y
let origin: Point = {x: 0.0, y: 0.0}
let delta: Point = {x: 1.5, y: 2.5}
[add_x(origin, delta), add_y(origin, delta)]
