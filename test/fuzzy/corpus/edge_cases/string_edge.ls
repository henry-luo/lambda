// String edge cases
// Tests boundary conditions, unicode, escapes, empty strings

// Empty strings
let empty1 = ""
let empty2 = len("")
let empty3 = split("", ",")
let empty4 = "" + ""

// Unicode and special characters
let unicode1 = "🔥🚀💻"
let unicode2 = "日本語"
let unicode3 = "Emoji: 👨‍👩‍👧‍👦"
let unicode4 = "Mixed: Hello 世界 🌍"

// Escape sequences
let escapes1 = "\n\t\r\\\"\'"
let escapes2 = "Line1\nLine2\nLine3"
let escapes3 = "Tab\tSeparated\tValues"

// Very short strings
let short1 = "a"
let short2 = "x"
let short3 = "!"

// String with only whitespace
let whitespace1 = "   "
let whitespace2 = "\n\n\n"
let whitespace3 = "\t\t\t"

// Special string patterns
let special1 = "\"quoted\""
let special2 = "'single'"
let special3 = "back\\slash"
let special4 = "null\0byte"

// String operations on edge cases
let op1 = ""[0]  // Should handle gracefully
let op2 = split("", "")
let op3 = replace("", "x", "y")
let op4 = trim("   ")

// String comparisons
let cmp1 = ("" == "")
let cmp2 = ("a" < "b")
let cmp3 = ("abc" == "abc")

[len(empty1), len(unicode1), len(escapes1), len(whitespace1), cmp1, cmp2, cmp3]
