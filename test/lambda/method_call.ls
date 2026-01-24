// Test file for method-style sys func calls
// Phase 1 & 2: obj.method() syntax for system functions

// Test 1: len() as method
"Test 1 - len as method:"
let arr = [1, 2, 3, 4, 5]
"arr.len() ="; arr.len()
"len(arr) ="; len(arr)

// Test 2: string type conversion as method  
"Test 2 - string() as method:"
let num = 42
"num.string() ="; num.string()

// Test 3: type() as method
"Test 3 - type() as method:"
"\"hello\".type() ="; "hello".type()

// Test 4: Math functions as methods
"Test 4 - math methods:"
let x = -5
"x.abs() ="; x.abs()

let y = 3.7
"y.round() ="; y.round()
"y.floor() ="; y.floor()
"y.ceil() ="; y.ceil()

// Test 5: Array aggregation methods
"Test 5 - array methods:"
let nums = [3, 1, 4, 1, 5, 9, 2, 6]
"nums.sum() ="; nums.sum()
"nums.min() ="; nums.min()
"nums.max() ="; nums.max()

// Test 6: String methods
"Test 6 - string methods:"
let s = "Hello, World!"
"s.len() ="; s.len()
"s.contains(\"World\") ="; s.contains("World")
"s.starts_with(\"Hello\") ="; s.starts_with("Hello")
"s.slice(0, 5) ="; s.slice(0, 5)

// Test 7: Array manipulation with args
"Test 7 - methods with args:"
let data = [10, 20, 30, 40, 50]
"data.take(3) ="; data.take(3)
"data.drop(2) ="; data.drop(2)

// Test 8: Chained method calls
"Test 8 - chained methods:"
"[5,3,1,4,2].sort() ="; [5, 3, 1, 4, 2].sort()
"[5,3,1,4,2].sort().reverse() ="; [5, 3, 1, 4, 2].sort().reverse()

// Test 9: Equivalence of both styles
"Test 9 - equivalence:"
let test = [1, 2, 3]
"len(test) ="; len(test)
"test.len() ="; test.len()
"sum(test) ="; sum(test)
"test.sum() ="; test.sum()
