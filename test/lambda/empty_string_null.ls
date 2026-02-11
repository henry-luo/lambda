// Test empty string literals are treated as null
// This tests the fix for compound boolean expressions in where clauses

"===== EMPTY STRING NULL TESTS ====="

// 1. Empty string should equal null
let empty = ""
"1. empty == null:"; (empty == null)
"2. type(empty):"; type(empty)
"3. empty == '':"; (empty == "")

// 2. Empty string in boolean context
"4. not empty:"; (not empty)
"5. empty or true:"; (empty or true)
"6. true and not empty:"; (true and not empty)

// 3. Empty string in where clause - the original bug
let data = [
    {name: "Alice", value: "hello"},
    {name: "Bob", value: ""},
    {name: "Charlie", value: "world"}
]

"7. Filter non-empty values:"
let non_empty = data where (~.value != "")
for (x in non_empty) x.name

// 4. Compound boolean with empty string
"8. Compound filter (value != '' and name != 'Alice'):"
let filtered = data where (~.value != "" and ~.name != "Alice")
for (x in filtered) x.name

// 5. Empty string comparison with other types
"9. empty != 'x':"; ("" != "x")
"10. empty != 0:"; ("" != 0)

// 6. Empty string in if-then-else
"11. if empty==null then 'yes' else 'no':"; (if ("" == null) "yes" else "no")

// 7. Nested empty string checks
let nested = [{a: {b: ""}}]
"12. Nested empty check:"; (nested[0].a.b == null)

"===== END EMPTY STRING NULL TESTS ====="
