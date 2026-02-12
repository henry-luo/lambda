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

// Test 4: inline match (single line, no braces)
fn sign_of(n: int) => match n case 0: "zero" default: "nonzero"

"Test 4: inline match"
sign_of(0)
sign_of(42)
sign_of(-1)

// Test 5: match in let expression
let x = match 42 case int: "is int" case string: "is string" default: "other"
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

// Test 7: nested match (inner match must use braces or be single-line delimited)
fn classify(value) => match value {
    case int: match value { case 0: "zero" default: "nonzero int" }
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
