// Test: Cross-Type Logical Operators
// Layer: 1 | Category: boundary | Covers: and, or, not across type pairs

// ===== and with bool × bool =====
true and true
true and false
false and true
false and false

// ===== or with bool × bool =====
true or true
true or false
false or true
false or false

// ===== not with bool =====
not true
not false

// ===== and with int × bool =====
1 and true
1 and false
0 and true
0 and false

// ===== or with int × bool =====
1 or true
1 or false
0 or true
0 or false

// ===== not with int =====
not 0
not 1
not 42
not -1

// ===== and with float × bool =====
3.14 and true
3.14 and false
0.0 and true
0.0 and false

// ===== or with float × bool =====
3.14 or true
3.14 or false
0.0 or true
0.0 or false

// ===== not with float =====
not 0.0
not 3.14
not -1.5

// ===== and/or with null =====
null and true
null and false
null or true
null or false
true and null
false and null
true or null
false or null

// ===== not with null =====
not null

// ===== and/or with array =====
[1] and true
[1] and false
[] and true
[] and false
[1] or true
[1] or false
[] or true
[] or false

// ===== not with array =====
not []
not [1]
not [1, 2, 3]

// ===== and/or with map =====
{a: 1} and true
{a: 1} and false
{a: 1} or true
{a: 1} or false

// ===== not with map =====
not {a: 1}
not {}

// ===== Mixed non-bool operands =====
1 and 2
0 and 0
1 or 2
0 or 0
null and null
null or null
[] and []
[] or []
