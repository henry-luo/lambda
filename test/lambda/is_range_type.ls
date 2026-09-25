// S11.1.3, S11.1.6v2 (LR02-24): the right side of `is` is a type, and two
// literals joined by `to` are one range type there, as in an annotation or a
// match arm. The C parser read `x is 1 to 5` as `(x is 1) to 5`. A bound that
// is not a literal (`x is 1 to n`) is no range type, in either parser.
let x = 3
let r = [x is 1 to 5, 9 is 1 to 5, 1 is 1 to 5, 5 is 1 to 5, 0 is 1 to 5]
r
// a range binds tighter than `and` and `or`
let both = x is 1 to 5 and x is 3 to 9
both
let either = 9 is 1 to 5 or 9 is 8 to 10
either
// character ranges
let chars = ["c" is "a" to "z", "C" is "a" to "z"]
chars
// `to` continues across a line break, and so does its end
let wrapped = [x is 1
    to 5, x is 1 to
    5]
wrapped
let grade = if (x is 1 to 5) "low" else "high"
grade
x is (1 to 5)
