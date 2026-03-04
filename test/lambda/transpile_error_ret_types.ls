// Test can_raise with various return types

// Section 1: float^ return type
fn safe_sqrt(x: float) float^ {
    if (x < 0.0) raise error("negative input")
    else x ** 0.5
}

fn test_float_ok1() { safe_sqrt(16.0)^ }
fn test_float_ok2() { safe_sqrt(25.0)^ }
fn test_float_err() {
    let val^err = safe_sqrt(0.0 - 1.0)
    if (^err) 99 else val
}

"1. float^"
[test_float_ok1(), test_float_ok2(), test_float_err()]

// Section 2: string^ return type
fn safe_greet(name: string) string^ {
    if (len(name) == 1) raise error("name too short")
    else "Hello " ++ name
}

fn test_str_ok1() { len(safe_greet("Alice")^) }
fn test_str_ok2() { len(safe_greet("Bob")^) }
fn test_str_err() {
    let val^err = safe_greet("x")
    if (^err) 99 else len(val)
}

"2. string^"
[test_str_ok1(), test_str_ok2(), test_str_err()]

// Section 3: bool^ return type
fn safe_gt(a: int, b: int) bool^ {
    if (a == 0 and b == 0) raise error("both zero")
    else (a > b)
}

fn test_bool_ok1() { safe_gt(10, 5)^ }
fn test_bool_ok2() { safe_gt(3, 7)^ }
fn test_bool_err() {
    let val^err = safe_gt(0, 0)
    if (^err) 99 else val
}

"3. bool^"
[test_bool_ok1(), test_bool_ok2(), test_bool_err()]

// Section 4: int^ return type
fn safe_div(a: int, b: int) int^ {
    if (b == 0) raise error("division by zero")
    else a div b
}

fn test_int_ok1() { safe_div(10, 3)^ }
fn test_int_ok2() { safe_div(100, 7)^ }
fn test_int_err() {
    let val^err = safe_div(10, 0)
    if (^err) 99 else val
}

"4. int^"
[test_int_ok1(), test_int_ok2(), test_int_err()]

// Section 5: Chained error propagation
fn compute_doubled_div(a: int, b: int) int^ {
    let d = safe_div(a, b)^
    d * 2
}

fn test_chain_ok() { compute_doubled_div(10, 2)^ }
fn test_chain_err() {
    let val^err = compute_doubled_div(10, 0)
    if (^err) 99 else val
}

"5. chained"
[test_chain_ok(), test_chain_err()]

// Section 6: let^err destructuring
fn parse_positive(x: int) int^ {
    if (x <= 0) raise error("not positive")
    else x
}

fn check_pair(a: int, b: int) {
    let x^e1 = parse_positive(a)
    let y^e2 = parse_positive(b)
    if (^e1) 91
    else if (^e2) 92
    else x + y
}

"6. let^err"
[check_pair(3, 4), check_pair(0, 4), check_pair(3, 0)]

// Section 7: or-default pattern
fn safe_val(x: int) int^ {
    if (x < 0) raise error("negative")
    else x * 2
}

fn with_default(x: int) {
    let val^err = safe_val(x)
    val or 0
}

"7. or-default"
[with_default(5), with_default(0 - 1)]
