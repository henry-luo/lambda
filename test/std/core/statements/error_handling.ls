// Test: Error Handling
// Layer: 2 | Category: statement | Covers: T^E, raise, ^, let a^err, compile enforcement

// ===== Basic error return type =====
fn safe_divide(a: int, b: int) int^ {
    if (b == 0) raise error("Division by zero")
    a / b
}
safe_divide(10, 2)
safe_divide(10, 0)

// ===== Error propagation with ^ =====
fn double_divide(a: int, b: int) int^ {
    let r = safe_divide(a, b)^
    r * 2
}
double_divide(10, 2)
double_divide(10, 0)

// ===== Let destructuring with ^err =====
let result^err = safe_divide(10, 0)
err
err.message

let good^err2 = safe_divide(10, 5)
good

// ===== Error with code =====
fn validate_age(age: int) int^ {
    if (age < 0) raise error("Age cannot be negative", code: "INVALID_AGE")
    if (age > 150) raise error("Age too large", code: "OUT_OF_RANGE")
    age
}
validate_age(25)
let v^e = validate_age(-5)
e.message
e.code

// ===== Chained errors =====
fn process_input(s: string) int^ {
    if (s == "") raise error("Empty input")
    let n = int(s)
    safe_divide(100, n)^
}
process_input("5")
process_input("0")

// ===== Error is falsy =====
let e = error("test error")
if (e) "truthy" else "falsy"

// ===== Error or default =====
fn get_value() int^ {
    raise error("not found")
}
let val = get_value() or 42
val

// ===== ^expr check =====
let check1 = ^safe_divide(10, 2)
check1
let check2 = ^safe_divide(10, 0)
check2

// ===== Multiple error returns =====
fn parse_and_divide(a_str: string, b_str: string) int^ {
    let a = int(a_str)
    let b = int(b_str)
    safe_divide(a, b)^
}
parse_and_divide("10", "2")
