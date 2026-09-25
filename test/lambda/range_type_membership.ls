// S11.1.3 (LR03-18, LR03-14): a range type admits the values between its
// bounds wherever it sits -- inside a union, a map or array type, an
// annotation or a parameter. It wore the range VALUE tag instead of the shared
// type tag (D3.1.1v4), so `is` never matched a range inside a larger type,
// range-typed parameters rejected every integer, and a range-typed map field
// read an int as a pointer.
type R = 1 to 5 | 10
type Low = 1 to 5
type Wide = 1 to 9
type Letter = "a" to "e"
type Bee = "b"
type Point = {x: 1 to 5, y: "a" to "e"}
type Gauge { level: 1 to 5 }

// `is` against unions, map types and array types that hold a range
let unions = [3 is R, 10 is R, 7 is R, 3 is 10 | 1 to 5, 12 is 1 to 5 | 10 to 20]
unions
let shapes = [{x: 3, y: "b"} is Point, {x: 9, y: "b"} is Point, [1, 5] is Low[], [1, 6] is Low[]]
shapes
let optional = [3 is Low?, null is Low?, 0 is Low?]
optional
// a member keeps its own carrier: `3.0` is in `1 to 5`, a range value is not
let members = [3.0 is Low, 3.5 is Low, "c" is Letter, "cc" is Letter, (1 to 5) is Low]
members

// annotations and parameters admit members
fn twice(x: Low) => x * 2
fn next(x: R) => x + 1
fn shout(ch: Letter) => ch ++ "!"
fn total(v: Low[]) => len(v)
fn px(p: Point) => p.x
fn as_int(n: int) => n + 100
fn relay(x: Low) => as_int(x)
let p: Point = {x: 4, y: "d"}
let g = <Gauge level: 3>
let calls = [twice(3), next(10), shout("b"), total([1, 2, 5]), px({x: 2, y: "a"}), relay(2), p.x, g.level]
calls

// subtyping by members (S11.1.4v2)
let relations = [Low <: Wide, Wide <: Low, Low <: int, Letter <: string, Low <: string, Bee <: Letter]
relations

// a union of ranges in a match arm
let arms = [for (n in [0, 3, 12, 7]) (match n { case 1 to 5 | 10 to 15: "in" default: "out" })]
arms
