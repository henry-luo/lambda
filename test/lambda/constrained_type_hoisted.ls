// Constrained types with the hoisted `that` clause (CT1v2/CT2/CT6/CT10).
// The predicate needs no parentheses: `that` sits at the top of an annotation,
// so nothing type-level can follow the predicate.

// === Unparenthesized predicates in every statement-level slot ===
type Positive = int that ~ > 0
type Percent = int that 0 <= ~ and ~ <= 100
type NonEmpty = string that len(~) > 0

let scored: int that 0 <= ~ <= 100 = 95
fn doubled(x: int that ~ > 0) int { x * 2 }

(5 is Positive); // expected: true
(0 is Positive); // expected: false
(50 is Percent); // expected: true
(150 is Percent); // expected: false
("hi" is NonEmpty); // expected: true
("" is NonEmpty)         // expected: false
scored
doubled(21)

// === Legacy parenthesized form still parses (CT7) ===
type LegacyPos = int that (~ > 0);
(3 is LegacyPos)         // expected: true

// === Match arms, both `:` and block bodies ===
fn classify(x) => match x {
  case int that ~ > 0 { "positive" }
  case int that ~ < 0: "negative"
  case 0: "zero"
  default: "other"
}
classify(7)
classify(-7)
classify(0)
classify("s")

// === CT2: the predicate is greedy — `| null` binds INTO the predicate,
// it is not a type union. `null is Tricky` is therefore false: Tricky is
// still a constrained int. Spell a union of a constrained type by naming it.
type Tricky = int that ~ > 0 | null;
(null is Tricky)         // expected: false
type Maybe = Positive | null;
(null is Maybe); // expected: true
(5 is Maybe)             // expected: true

// === CT10: named alias composes into containers ===
type Scores = [Positive];
([5] is Scores)          // expected: true

// === CT6: object-level constraint parses without parens ===
// Both arms are true because object/element constraint PREDICATES are not yet
// evaluated (SO9; lambda-eval.cpp keeps `is` base-type-only until validator
// predicate evaluation ships). The parenthesized form behaves identically, so
// this pins the parse, not the enforcement.
type Range {
    start: int,
    end: int that ~ >= 0;
    that ~.end > ~.start
}
let range_ok = <Range start: 1, end: 5>
let range_bad = <Range start: 5, end: 1>
range_ok is Range        // expected: true
range_bad is Range       // expected: true (predicate not evaluated — SO9)
