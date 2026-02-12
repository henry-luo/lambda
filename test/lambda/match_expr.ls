// Match expression tests

// Test 1: type patterns (braced form)
fn describe(value: int | string | bool) => match value {
    case int: "integer"
    case string: "text"
    case bool: "boolean"
}

"Test 1: type patterns"
describe(42)
describe("hello")
describe(true)

// Test 2: literal patterns (braced form)
fn status_text(code: int) => match code {
    case 200: "OK"
    case 404: "Not Found"
    case 500: "Server Error"
    default: "Unknown"
}

"Test 2: literal patterns"
status_text(200)
status_text(404)
status_text(500)
status_text(999)

// Test 3: symbol patterns (braced form)
fn color_of(level) => match level {
    case 'info': "blue"
    case 'warn': "yellow"
    case 'error': "red"
    default: "white"
}

"Test 3: symbol patterns"
color_of('info')
color_of('warn')
color_of('error')
color_of('unknown')

// Test 4: braced match in function
fn sign_of(n: int) => match n {
    case 0: "zero"
    default: "nonzero"
}

"Test 4: braced match"
sign_of(0)
sign_of(42)
sign_of(-1)

// Test 5: match in let expression
let x = match 42 {
    case int: "is int"
    case string: "is string"
    default: "other"
}
"Test 5: let match"
x

// Test 6: match with null pattern
fn check_null(v) => match v {
    case null: "null value"
    default: "has value"
}

"Test 6: null pattern"
check_null(null)
check_null(42)

// Test 7: nested match (both matches use braces)
fn classify(value) => match value {
    case int: match value {
        case 0: "zero"
        default: "nonzero int"
    }
    case string: "string"
    default: "other"
}

"Test 7: nested match"
classify(0)
classify(5)
classify("hi")
classify(true)

// Test 8: match with current item reference (~)
fn check_range(n: int) => match n {
    case 0: "zero"
    case int: if (~ > 0) "positive" else "negative"
}

"Test 8: current item ~"
check_range(0)
check_range(10)
check_range(-5)

// Test 9: match with bool literal patterns
fn bool_match(v) => match v {
    case true: "is true"
    case false: "is false"
    default: "not bool"
}

"Test 9: bool patterns"
bool_match(true)
bool_match(false)
bool_match(42)

// Test 10: match with float patterns
fn float_match(v) => match v {
    case 0.0: "zero float"
    case float: "some float"
    default: "not float"
}

"Test 10: float patterns"
float_match(0.0)
float_match(3.14)
float_match(42)

// Test 11: match with range patterns
fn grade(score: int) => match score {
    case 90 to 100: "A"
    case 80 to 89: "B"
    case 70 to 79: "C"
    case 60 to 69: "D"
    default: "F"
}

"Test 11: range patterns"
grade(95)
grade(85)
grade(75)
grade(65)
grade(50)

// Test 12: match range type keyword
fn check_type(v) => match v {
    case range: "is range"
    case int: "is int"
    default: "other"
}

"Test 12: range type"
check_type(1 to 10)
check_type(42)
check_type("hi")

// Test 13: match range with or-patterns
fn http_status(code: int) => match code {
    case 200 to 299: "success"
    case 300 to 399: "redirect"
    case 400 to 499 | 500 to 599: "error"
    default: "unknown"
}

"Test 13: range or-patterns"
http_status(200)
http_status(301)
http_status(404)
http_status(503)
http_status(100)
