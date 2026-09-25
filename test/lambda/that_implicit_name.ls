// Test: implicit ~.name resolution in a 'that' body
// In a 'that' proviso (S10.1.5v3), a bare identifier not in scope resolves to
// ~.name, where ~ is the proviso's left operand. The '|:' filter (S10.1.6) is
// a pipe stage and never does this: its body names ~ explicitly.
// Name resolution order: 1) scope names, 2) ~.name, 3) system properties

// ============================================================
// Section 1: Basic expr-level 'that' with a map
// ============================================================

'=1a=';
// Proviso on a field - explicit ~.name
{name: "alice", age: 30} that (~.age > 28)

'=1b=';
// Same proviso using implicit name resolution (age instead of ~.age)
{name: "alice", age: 30} that (age > 28)

'=1c=';
// Access multiple fields implicitly; a failed proviso is null
{name: "alice", age: 30} that (age >= 25 and name == "bob")

// ============================================================
// Section 2: Scope names take priority over field names
// ============================================================

'=2a='
// let binding 'age' in scope should win over ~.age, so 25 > 28 is false
let age = 25;
{name: "alice", age: 30} that (age > 28)

'=2b='
// 'age2' refers to the scope variable (25), not a field
let age2 = 25;
{name: "alice", age: 30} that (age2 > 20)

// ============================================================
// Section 3: A '|:' filter names ~ explicitly
// ============================================================

'=3a=';
[{x: 1, y: 2}, {x: 3, y: 4}] |: (~.x > 2)

'=3b=';
[{x: 10, y: 20}, {x: 30, y: 40}] |: (~.x + ~.y > 50)

// ============================================================
// Section 4: Object-level that-constraint with implicit name
// (Tested separately in map_object_robustness.ls)
// ============================================================

// ============================================================
// Section 5: Mix of ~ and implicit names
// ============================================================

'=5a=';
// Can still use ~ explicitly alongside implicit names
{name: "alice", age: 30} that (name == "alice" and ~.age == 30)

'=5b=';
// A proviso inside a filter body reads its own ~ implicitly
[{a: 1, b: 2}, {a: 3, b: 4}] |: ((~ that a > 2) != null)

// ============================================================
// Section 6: A called name is a function, never a field
// ============================================================

'=6a=';
// len is the system function; items is ~.items
{items: [1, 2, 3]} that (len(items) == 3)

'=6b=';
[[1, 2, 3], [1]] that len(~) > 1
