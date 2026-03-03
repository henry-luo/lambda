// Test: expr is nan — IEEE NaN detection
// Test: expr is value — value equality via 'is'

// 1. Basic nan check
"1. Basic nan check"
[nan is nan, (0.0/0.0) is nan, (0/0) is nan]

// 2. Non-nan values
"2. Non-nan values"
[1.0 is nan, 0 is nan, inf is nan, -inf is nan, "hello" is nan, null is nan, true is nan]

// 3. NaN from math operations
"3. NaN from math operations"
[math.log(-1) is nan, math.sqrt(-1) is nan, (inf - inf) is nan]

// 4. nan == nan follows IEEE (false), but is nan catches it
"4. nan equality vs is nan"
[nan == nan, nan is nan, nan != nan]

// 5. Replace NaN with 0
"5. Replace NaN"
[1, 2, nan, 4, nan] | (if (~ is nan) 0 else ~)

// 6. Value equality via 'is'
"6. Value equality"
[42 is 42, 42 is 43, true is true, false is true]

// 7. Float, string, array value equality
"7. Float/string/array"
[inf is inf, "hello" is "hello", "hello" is "world", [1,2,3] is [1,2,3], [1,2,3] is [1,2,4]]

// 8. Type checks still work
"8. Type checks"
[42 is int, "hi" is string, null is null, [1,2] is array]
