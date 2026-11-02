// S10.1.7v2: a body that binds `~` to one subject may leave `~` implicit, so a
// bare field name reads `~.name` of that body's current item. A match arm, a
// handler's value arm and a constrained arm (`T that cond`) align with the
// `that` proviso: `~` is the current item, spelled or implicit, scoped to the
// body, and outside the body the outer binding is back. The pipe family
// (`|>`, `|:`) always spells `~`.
import math
import m: math

fn lookup(record, fail) any^ {
    if (fail) raise error("missing")
    else record
}

// ============================================================
// Section 1: A match arm reads the matched value's fields
// ============================================================

'=1a=';
match {age: 20, name: "ann"} { case map: age > 18 default: false }

'=1b=';
// ~ may still be spelled
match {age: 20} { case map: ~.age + 1 default: 0 }

'=1c=';
// a binding claims its name before the matched value's field does
let limit = 18;
match {age: 20, limit: 99} { case map: age > limit default: false }

'=1d=';
// the default arm reads the same current item
match {size: 3} { case int: 0 default: size * 2 }

// ============================================================
// Section 2: A handler's value arm reads its result's fields
// ============================================================

'=2a=';
{ let __error_outcome_1 = lookup({total: 5, tax: 1}, false) ^ { ^ }; if (__error_outcome_1 is error) { 0 } else match __error_outcome_1 { case any { total + tax } } }

'=2b=';
// the error arm binds ^ and keeps the outer ~: fallback is the subject's
{fallback: 7} that (({ let __error_outcome_2 = lookup({}, true) ^ { ^ }; if (__error_outcome_2 is error) { fallback } else match __error_outcome_2 { case any { 0 } } }) == 7)

// ============================================================
// Section 3: A constrained arm reads the candidate's fields
// ============================================================

'=3a=';
match {a: 3} { case {a: int} that (a > 1): "big" default: "small" }

'=3b=';
match {a: 0} { case {a: int} that (a > 1): "big" default: "small" }

'=3c=';
// the same in any position: a constrained alias checked by `is`
type Adult = {age: int} that (age >= 18);
[{age: 20} is Adult, {age: 5} is Adult]

// ============================================================
// Section 4: Each body scopes its own current item
// ============================================================

'=4a=';
// in the arm ~ is kind, so total reads kind.total; after the match ~ is the
// subject again, and total reads the subject's own field
{kind: {total: 99}, total: 5} that ((match kind { case map: total default: 0 }) + total == 104)

'=4b=';
// the same for a handler's value arm
{rec: {total: 7}, total: 1} that (({ let __error_outcome_3 = lookup(rec, false) ^ { ^ }; if (__error_outcome_3 is error) { 0 } else match __error_outcome_3 { case any { total } } }) + total == 8)

'=4c=';
// and for a constrained arm: lim is the candidate's, then the arm's
{v: {lim: 3}, lim: 100} that (match v { case {lim: int} that (lim < 10): lim default: 0 } == 3)

// ============================================================
// Section 5: The pipe family still spells ~
// ============================================================

'=5a=';
// a match on ~ inside a pipe body: the arm reads each item's fields
[{total: 1}, {total: 2}] |> match (~) { case map: total * 10 default: 0 }

'=5b=';
// a pipe body inside an arm reads bare names as names: len is the system
// function applied to items, and items is the matched value's ~.items
match {items: [{len: 5}, {len: 6}]} { case map: items |> len default: 0 }

// ============================================================
// Section 6: Bindings claim a name before the current item does
// ============================================================

'=6a=';
// `import math` binds pi, so pi is the constant, not the field ~.pi
match {r: 2.0, pi: 3} { case map: pi * r * r > 12.0 default: false }

'=6b=';
// a module alias keeps its qualified meaning in an arm
match 16 { case int: m.sqrt(~) default: 0 }

'=6c=';
// a called name is a function, never a field
match {len: 99, xs: [1, 2]} { case map: len(xs) default: 0 }
