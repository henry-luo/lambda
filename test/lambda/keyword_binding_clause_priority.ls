// S16.10.1v2: continuation-only words are legal binding names, and the clause
// reading keeps priority — the enclosing if/match/for claims the word before
// an expression is parsed, so both readings coexist in one expression.
let case = 2
let default = 4
let else = 5
let on = 3

// the arm keyword dispatches (7 is int), then `case` reads as the binding:
// a fall-through to the default arm would yield 0 instead of 3.
match 7 { case int: case + 1 default: 0 }

// `else` parses as the if-clause, `default` as the binding.
if (false) 1 else default

// ordinary expression position. NB: a line may not START with `else`, which
// is an S16.2.2v2 continuation token — that caveat is independent of this bar.
on + else

// for-header clause words keep working alongside same-named bindings.
for (x in [3, 1, 2] order by x limit 2) x
