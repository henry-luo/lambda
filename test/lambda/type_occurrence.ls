// Type Occurrence Pattern Feature Test Suite
// Tests the type occurrence count syntax: T[n], T[min, max], T[n+]

"===== TYPE OCCURRENCE PATTERN TESTS ====="

// ============================================================
// Test 1: Basic Type Occurrence - Exact Count [n]
// ============================================================
'Test 1: Exact Count [n]'

// Type definition with exact count
type TwoInts = int[2]
type ThreeStrings = string[3]

// Testing exact count matches
"1.1"; ([1, 2] is TwoInts)             // true - exactly 2 ints
"1.2"; ([1] is TwoInts)                // false - only 1 int
"1.3"; ([1, 2, 3] is TwoInts)          // false - 3 ints, need 2
"1.4"; (["a", "b", "c"] is ThreeStrings) // true - exactly 3 strings
"1.5"; (["a", "b"] is ThreeStrings)    // false - only 2 strings

// ============================================================
// Test 2: Range Count [min, max]
// ============================================================
'Test 2: Range Count [min, max]'

type TwoToFourInts = int[2, 4]
type OneToThreeStrings = string[1, 3]

"2.1"; ([1] is TwoToFourInts)          // false - too few
"2.2"; ([1, 2] is TwoToFourInts)       // true - minimum bound
"2.3"; ([1, 2, 3] is TwoToFourInts)    // true - within range
"2.4"; ([1, 2, 3, 4] is TwoToFourInts) // true - maximum bound
"2.5"; ([1, 2, 3, 4, 5] is TwoToFourInts) // false - too many

"2.6"; ([] is OneToThreeStrings)       // false - too few
"2.7"; (["a"] is OneToThreeStrings)    // true - minimum bound
"2.8"; (["a", "b", "c"] is OneToThreeStrings) // true - maximum bound
"2.9"; (["a", "b", "c", "d"] is OneToThreeStrings) // false - too many

// ============================================================
// Test 3: Unbounded Minimum [n+]
// ============================================================
'Test 3: Unbounded Minimum [n+]'

type AtLeastTwoInts = int[2+]
type AtLeastOneString = string[1+]

"3.1"; ([1] is AtLeastTwoInts)         // false - only 1
"3.2"; ([1, 2] is AtLeastTwoInts)      // true - exactly 2
"3.3"; ([1, 2, 3, 4, 5, 6, 7, 8, 9, 10] is AtLeastTwoInts) // true - many is ok

"3.4"; ([] is AtLeastOneString)        // false - empty
"3.5"; (["a"] is AtLeastOneString)     // true - exactly 1
"3.6"; (["a", "b", "c", "d"] is AtLeastOneString) // true - more is ok

// ============================================================
// Test 4: Standard Occurrence Operators with New Syntax
// ============================================================
'Test 4: Standard Occurrence Operators'

// These should still work with the existing ?, +, * syntax
type MaybeInt = int?
type OneOrMoreInts = int+
type ZeroOrMoreInts = int*

"4.1"; (null is MaybeInt)              // true - null matches optional
"4.2"; (1 is MaybeInt)                 // true - value matches optional
"4.3"; ([1, 2] is OneOrMoreInts)       // true - multiple
"4.4"; ([] is OneOrMoreInts)           // false - zero not allowed
"4.5"; ([] is ZeroOrMoreInts)          // true - zero allowed
"4.6"; ([1, 2, 3] is ZeroOrMoreInts)   // true - any count

// ============================================================
// Test 5: Edge Cases and Special Values
// ============================================================
'Test 5: Edge Cases'

type ZeroCount = int[0]
type ZeroOrOne = int[0, 1]
type LargeCount = int[10]

"5.1"; ([] is ZeroCount)               // true - exactly 0
"5.2"; ([1] is ZeroCount)              // false - has 1
"5.3"; ([] is ZeroOrOne)               // true - 0 is in range
"5.4"; ([1] is ZeroOrOne)              // true - 1 is in range
"5.5"; ([1, 2] is ZeroOrOne)           // false - 2 is out of range

// ============================================================
// Test 6: Mixed Types in Lists
// ============================================================
'Test 6: Type Checking with Mixed Content'

type IntList = int[2]

"6.1"; ([1, "a"] is IntList)           // false - contains non-int
"6.2"; ([1, 2] is IntList)             // true - exactly 2 ints
"6.3"; (["a", "b"] is IntList)         // false - wrong element type

// ============================================================
// Test 7: Nested Type Patterns
// ============================================================
'Test 7: Nested Patterns'

type PairOfLists = int*[2]  // exactly 2 lists of ints

"7.1"; ([[1, 2], [3, 4, 5]] is PairOfLists)  // true - 2 int lists
"7.2"; ([[1]] is PairOfLists)                // false - only 1 list
"7.3"; ([[1, 2], [3], [4]] is PairOfLists)   // false - 3 lists

// ============================================================
// Test 8: Inline Type Occurrence
// NOTE: Inline occurrence syntax like `int[3]` is currently not supported
// directly in expressions due to grammar ambiguity with array indexing.
// Use type aliases instead (e.g., `type T = int[3]`)
// ============================================================
'Test 8: Inline Type Occurrence (via alias workaround)'

// Inline occurrence must use type aliases
type InlineThreeInts = int[3]
type InlineRangeStrings = string[2, 4]
type InlineAtLeastTwoInts = int[2+]

"8.1"; ([1, 2, 3] is InlineThreeInts)     // true
"8.2"; ([1, 2] is InlineThreeInts)        // false
"8.3"; (["a", "b"] is InlineRangeStrings) // true
"8.4"; ([1, 2, 3] is InlineAtLeastTwoInts) // true

"===== END OF TYPE OCCURRENCE TESTS ====="
