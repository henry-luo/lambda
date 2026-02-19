// Test: // line comments are parsed correctly after various expressions
// This covers the fix where // could be parsed as division operator

"1. Comment after literals"
42 // comment after integer
3.14 // comment after float
"hello" // comment after string
true // comment after bool
'sym' // comment after symbol

"2. Comment after arithmetic"
10 + 20 // comment after addition
10 - 3 // comment after subtraction
4 * 5 // comment after multiplication
10 / 2 // comment after division (the key case!)
10 % 3 // comment after modulo

"3. Comment after let bindings"
let a = 100 // comment after let
let b = a + 1 // comment after let with expr
b

"4. Comment after containers"
[1, 2, 3] // comment after array
{x: 1, y: 2} // comment after map
<div id: "test"> // comment after element

"5. Comment after function calls"
abs(-5) // comment after call
string(42) // comment after conversion
len([1, 2, 3]) // comment after len

"6. Comment after member/index access"
let m = {a: 10, b: 20}
m.a // comment after member access
let arr = [10, 20, 30]
arr[1] // comment after index access

"7. Comment after parenthesized expressions"
(3 + 4) // comment after parens
(10 / 2) // comment after division in parens

"8. Comment after for comprehension"
for (x in [1, 2, 3]) x * 10 // comment after for body

"9. Comment after if expression"
if (true) 100 else 200 // comment after if-else

"10. Comment after element in fn block"
fn make_el(id) {
    <span id: id> // comment inside fn block
}
make_el("test123")

"11. Comment after fn => expression"
fn double(x) => x * 2 // comment after arrow fn
double(7)

"12. Comment after closing brace"
fn add(x, y) {
    x + y
} // comment after closing brace
add(3, 4)

"13. Comment after pipe"
[1, 2, 3] | ~ + 10 // comment after pipe

"14. Comment no space before //"
42// no space before comment

"15. Division then comment on same line"
let d = 100 / 4 // this is division, then comment
d

"16. Multiple comments in sequence"
// first comment
// second comment
// third comment
99

"17. Comment after nested elements"
<ul; <li; "item1">; <li; "item2">> // comment after nested
