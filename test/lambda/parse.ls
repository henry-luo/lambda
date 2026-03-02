// parse() function tests

// Test 1: Parse JSON with explicit format
let r1^err1 = parse("{\"name\": \"Alice\", \"age\": 30}", 'json')
r1.name
r1.age

// Test 2: Parse JSON auto-detect
let r2^err2 = parse("{\"x\": 1}")
r2.x

// Test 3: Parse JSON array
let r3^err3 = parse("[1, 2, 3]", 'json')
r3
