// S11.4.10, S11.4.1v3 (LR03-20): an object literal is a nominal binding, so
// each field meets its declared contract. A value the compiler cannot prove is
// admitted at construction, converting as any boundary does, and a failure is
// the error the enclosing function returns. Construction used to store any
// value unchecked: a string's pointer read back from an int field.
// A range or literal-union field is boxed, so each is declared last: a boxed
// field followed by another reads back wrong in an object type (LR03-19).
type Point { x: float, y: float }
type Grade { name: string, level: 1 to 5 }
type Mark { name: string, mark: "A" | "B" }
type Named { name: string, nick: string? }
type Animal { name: string, legs: int = 4 }
type Dog : Animal { tail: bool = true }
type Pair { a: int, b: string }
type Holder { v: any }
fn dyn(v) => v
fn make_grade(level) => <Grade name: "g", level: level>

// an int widens into a float field; members of range and literal fields pass
let p = <Point x: 1, y: 2.5>
let g = <Grade name: "g", level: dyn(3)>
let m = <Mark name: "m", mark: dyn("B")>
let fields = [p.x, type(p.x), g.level, m.mark]
fields

// an exactly integral float is re-represented in an int field (S11.4.5)
let pair = <Pair a: dyn(3.0), b: "three">
let exact = [pair.a, type(pair.a)]
exact

// an omitted optional field is null, and an inherited default applies
let n = <Named name: "Ada">
let d = <Dog name: "Rex">
let defaults = [n.nick, d.legs, d.tail]
defaults

// a spread supplies its fields, which are admitted like written ones
let src = {a: 4, b: "four"}
let spread = <Pair *: src>
let supplied = [spread.a, spread.b]
supplied

// an `any` field keeps an error value
let h = <Holder v: int("x")>
h.v is error

// a failed field makes the enclosing function return its error
let results = [make_grade(2).level, make_grade(9) is error]
results
