// Guardrail test for the type-pattern grammar/scanner work: pins the parse
// boundaries an external pattern scanner must reproduce exactly.

let data = [1, "a", 2]
let other = [9]

// E1: query takes a single primary type; a following `|` is a VALUE union,
// never part of the queried type.
let e1 = data?int | other

// E2: annotation continues across a newline that follows a trailing operator
let e2: int |
  string = 42

// E3: annotation continues across a newline that precedes a binary type operator
let e3: int
  | string = 7

// E4: occurrence forms — counted, nullable-array, and explicit grouping
type Trip = int[3]
type NullArr = int?[]
type Grouped = (int*)[2]
let e4: int[3] = [1, 2, 3]

// E5: string/symbol pattern islands in value position
let isl = \(d[3])
let e5 = ("123" is isl)

// E6: constrained type via `that` (parenthesized predicate form)
type Pos = int that (~ > 0)
let e6 = (5 is Pos)

// E7: constrained type in a match arm, with both `:` and block bodies
fn classify(x) => match x {
  case int that (~ > 0) { "pos" }
  case int: "int"
  default: "other"
}

// E8: comment inside a nested map annotation
let e8: {a: int, // inline comment
  b: string} = {a: 1, b: "z"}

// E9: nested container and function-type annotations
let e9: {a: int, b: [string]} = {a: 1, b: ["x"]}
let fnty: fn(a: int) int = (a: int) int => a

// E10: a type alias terminates at the line end — the next line is a statement
type Alias = int;
[3];

[e1, e2, e3, e4, e5, e6, classify(5), classify(-1), classify("s"), e8.a, e9.a, fnty(7)]
