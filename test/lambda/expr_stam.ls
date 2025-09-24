// Comprehensive test for let/if/for expressions vs statements
// Testing all Lambda Script data types and complex combinations

// === BASIC FOR EXPRESSION ===
"# Fixed for expressions with arrays"
(for (x in [1, 2, 3]) x * 2)
(for (item in ["a", "b", "c"]) item + "!")
(for (num in [1, 2, 3, 4, 5]) (if (num % 2 == 0) num else 0))

"# Fixed for expressions with ranges"
(for (i in 1 to 5) i * i)
(for (j in 0 to 3) j + 10)

"# For statements with arrays"
for item in [4, 5, 6] {
    item * 2
}
for i in 1 to 10 {
    i + 5
}

// === BASIC LET EXPRESSIONS vs STATEMENTS ===
"# Let expressions with scalar types"
(let x = 42, x + 1)  // int
(let y = 3.14, y * 2)  // float  
(let z = true, not z)  // bool
(let s = "hello", s + " world")  // string
(let sym = 'test', sym)  // symbol
(let dt = t'2025-01-01', dt)  // datetime
(let bin = b'\xDEADBEEF', bin)  // binary
(let n = null, n)  // null

'# Let statements with scalar types'
let a = 42, b = 3.14, c = true;
let str1 = "hello", str2 = " world";
let symbol1 = 'test1', symbol2 = 'test2';
let date1 = t'2025-01-01', date2 = t'2025-12-31';
let binary1 = b'\xDEAD', binary2 = b'\xBEEF';
let null_val = null;

// === COMPLEX DATA STRUCTURE TESTS ===
'# Let expressions with arrays'
(let arr = [1, 2, 3], arr[1])
(let nested = [[1, 2], [3, 4]], nested[0][1])
(let mixed = [1, "two", 3.0, true, null], mixed[2])

'# Let statements with arrays'
let numbers = [1, 2, 3, 4, 5];
let strings = ["a", "b", "c"];
let bools = [true, false, true];
let mixed_array = [42, "hello", 3.14, true, null, [1, 2]];

'# Let expressions with maps'
(let m = {a: 1, b: 2}, m.a + m.b)
(let person = {name: "Alice", age: 30}, person.name)
(let nested_map = {outer: {inner: 42}}, nested_map.outer.inner)

"# Let statements with maps"
let config = {debug: true, port: 8080, host: "localhost"};
let user = {id: 123, name: "Bob", email: "bob@test.com", active: true};
let complex_map = {
    data: [1, 2, 3],
    meta: {version: "1.0", author: "test"},
    flags: {enabled: true, debug: false}
};

// Let expressions with lists
(let lst = (1, 2, 3), lst)

// === IF EXPRESSION vs STATEMENT TESTS ===
"# Simple if expressions"
(if (true) 'yes' else 'no')
(if (1 > 0) 42 else 0)
(if (false) null else 'default')

"# Nested if expressions"
(let choice = 1, if (choice == 1) 42 else if (choice == 2) 'string' else 'other')

"# If expressions with let"
(let x = 5, if (x > 3) 'big' else 'small')
(let score = 85, if (score >= 90) 'A' else if (score >= 80) 'B' else 'C')

"# Nested if expressions"
(if (true) (if (false) 1 else 2) else 3)
(let val = 10, if (val > 5) (if (val > 15) 'very big' else 'medium') else 'small')

"# If statements"
if (a > 40) {
    'a is greater than 40'
}
if (str1 == "hello") {
    str1 + str2
} else {
    "no match"
}

'user:'
if (user.active) {
    user.name + " is active"
} else {
    user.name + " is inactive"
}

// === FOR EXPRESSION vs STATEMENT TESTS ===
'# For expressions with arrays'
(for (x in [1, 2, 3]) x * 2)
(for (item in strings) item + "!")
(for (num in numbers) (if (num % 2 == 0) num else 0))

"# For expressions with ranges"
(for (i in 1 to 5) i * i)
(for (j in 0 to 3) j + 10)

"# For statements with arrays"
for item in mixed_array {
    item
}

for num in numbers {
    if (num > 3) {
        num * 10
    } else {
        num
    }
}

"# For statements with ranges"
for i in 1 to 10 {
    i * 2 + 1
}

for index in 0 to 4 {
    numbers[index] + index
}

// === COMPLEX COMBINATIONS ===

"# Let with if expression inside"
(let data = [1, 2, 3, 4, 5], 
 let filtered = (for (x in data) if (x % 2 == 0) x else 0),
 filtered)

'# If expression with let expressions in branches'
(if ((let temp = 25, temp > 20)) 
    (let msg = "warm", msg + " weather")
 else 
    (let msg = "cool", msg + " weather"))

'# For expression with if expressions'
(for (n in 1 to 10) 
    if (n % 3 == 0) "fizz" 
    else if (n % 5 == 0) "buzz" 
    else n)

'# Nested let expressions'
(let x = (let a = 10, let b = 20, a + b),
 let y = (let c = x * 2, c + 5),
 x + y)

"# Complex nested statements"
let matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];

for row in matrix {
    for col in row {
        if (col % 2 == 0) {
            col * 10
        } else {
            col
        }
    }
}

"# Let statement with complex initialization"
let complex_init = {
    numbers_squared: (for (i in 1 to 5) i * i),
    strings_doubled: (for (s in ["a", "b", "c"]) s + s),
    conditions: (for (x in [1, 2, 3, 4]) (if (x > 2) true else false))
};
complex_init

"# If statement with for expressions in branches"
if (numbers[0] > 0) {
    (for (n in numbers) n + 100)
} else {
    (for (n in numbers) n - 100)
}

// === TYPE MIXING AND EDGE CASES ===
"# Let with different types in sequence"
(let i = 42, let f = 3.14, let s = "test", let b = true,
 [i, f, s, b])

"# If expressions returning different types (should unify)"
(let choice = 1,
 if (choice == 1) 42
 else if (choice == 2) "string" 
 else if (choice == 3) 3.14
 else true)

"# For with different collection types"
(for (item in (1, "two", 3.0)) item)  // list
(for (val in [null, true, false, 123]) val)  // array

"=== EDGE CASES AND ERROR CONDITIONS ==="

'# Empty collections'
(for (x in []) x)  // empty array
(for (y in {}) y)  // empty map

'# Null handling'
(let maybe_null = null, if (maybe_null) "has value" else "is null")
(for (item in [1, null, 3]) (if (item) item else "empty"))

'# Zero and negative numbers'
(let zero = 0, if (zero) 'truthy' else 'falsy')
(let negative = -5, if (negative > 0) 'positive' else 'not positive')
(for (n in -2 to 2) (if (n == 0) "zero" else n))

"# Very nested expressions"
(let a = 1,
 let b = (let x = a + 1, x * 2),
 let c = (if (b > 3) (for (i in 1 to b) i) else [b]),
 (for (item in c) item + 10))

"# Mixed statement and expression patterns"
let base_value = 100;
if (base_value > 50) {
    let multiplier = (if (base_value > 75) 2 else 1);
    for i in 1 to 5 {
        let result = (let temp = base_value * multiplier, temp + i);
        result
    }
} else {
    base_value
}

"# Final complex example combining everything"
let final_test = {
    data: (for (i in 1 to 3) {value: i, squared: i * i}),
    process: (let threshold = 2,
        for (item in (for (j in 1 to 3) {value: j, squared: j * j}))
            if (item.value > threshold) 
                {result: item.squared * 10, status: "high"}
            else 
                {result: item.squared, status: "low"})
};

final_test.process
