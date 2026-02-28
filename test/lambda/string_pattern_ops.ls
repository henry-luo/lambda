// String Pattern Operations Test Suite
// Tests find(), replace(), split() with pattern arguments

'===== STRING PATTERN OPS TESTS ====='

// ============================================================
// Test 1: replace(str, pattern, repl) — pattern-based replace
// ============================================================
'Test 1: Pattern Replace'

string digit = \d
string digits = \d+
string ws = \s+

1; replace("a1b2c3", digit, "X")           // "aXbXcX"
2; replace("a1b22c333", digits, "N")        // "aNbNcN"
3; replace("hello   world", ws, " ")        // "hello world"
4; replace("no-digits", digit, "X")         // "no-digits" (no match)
5; replace("abc", digit, "")                // "abc" (no match, empty repl)
6; replace("a1b2", digit, "")               // "ab" (match, delete digits)

// ============================================================
// Test 2: split(str, pattern) — pattern-based split
// ============================================================
'Test 2: Pattern Split'

7; split("a1b2c3", digit)                   // ["a", "b", "c", ""]
8; split("hello   world", ws)               // ["hello", "world"]
9; split("a1b22c333", digits)               // ["a", "b", "c", ""]
10; split("no-match", digit)                // ["no-match"]

// ============================================================
// Test 3: split(str, pattern, true) — keep delimiters
// ============================================================
'Test 3: Pattern Split Keep Delimiters'

11; split("a1b2c3", digit, true)            // ["a", "1", "b", "2", "c", "3", ""]
12; split("hello   world", ws, true)        // ["hello", "   ", "world"]

// ============================================================
// Test 4: find(str, pattern) — pattern-based find
// ============================================================
'Test 4: Pattern Find'

string words = \w+
13; find("a1b22c333", digits)
14; find("no-match-here", digits)
15; find("hello world", words)

// ============================================================
// Test 5: find(str, string) — plain string find
// ============================================================
'Test 5: Plain String Find'

16; find("hello world hello", "lo")
17; find("aaa", "a")
18; find("no match", "xyz")

// ============================================================
// Test 6: split(str, string, true) — string split with keep delimiters
// ============================================================
'Test 6: String Split Keep Delimiters'

19; split("a,b,c", ",", true)
20; split("a::b::c", "::", true)

// ============================================================
// Test 7: replace with empty string (edge cases)
// ============================================================
'Test 7: Replace Empty String'

21; replace("abc", "b", "")                 // "ac" (delete match)
22; replace("aaa", "a", "")                 // "" (delete all)
23; replace("hello", "x", "")              // "hello" (no match)
