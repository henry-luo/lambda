// Test: Error Safe Pipeline
// Layer: 4 | Category: integration | Covers: error handling, T^E, let a^err, or default

// ===== Define fallible functions =====
fn parse_int(s: string) int^ {
    let n = int(s)
    if (n is error) raise error("Cannot parse: " & s, code: "PARSE_ERR")
    n
}

fn safe_divide(a: int, b: int) int^ {
    if (b == 0) raise error("Division by zero", code: "DIV_ZERO")
    a / b
}

fn validate_positive(n: int) int^ {
    if (n <= 0) raise error("Must be positive: " & str(n), code: "INVALID")
    n
}

// ===== Successful pipeline =====
fn process(a_str: string, b_str: string) string^ {
    let a = parse_int(a_str)^
    let b = parse_int(b_str)^
    let result = safe_divide(a, b)^
    let valid = validate_positive(result)^
    "Result: " & str(valid)
}
process("100", "5")

// ===== Error at parse stage =====
let r1^e1 = process("abc", "5")
e1.message
e1.code

// ===== Error at divide stage =====
let r2^e2 = process("10", "0")
e2.message
e2.code

// ===== Error at validate stage =====
let r3^e3 = process("-10", "1")
// -10 / 1 = -10, which is not positive
e3.message

// ===== Or default pattern =====
let safe1 = process("10", "2") or "default"
safe1

let safe2 = process("abc", "1") or "fallback"
safe2

// ===== ^expr check =====
^process("10", "2")
^process("abc", "1")

// ===== Error chain =====
fn wrapped_process(a: string, b: string) string^ {
    let result^err = process(a, b)
    if (err) raise error("Process failed", source: err)
    result
}
let wr^we = wrapped_process("abc", "1")
we.message
we.source.message

// ===== Batch processing with error handling =====
let inputs = [("10", "2"), ("abc", "1"), ("20", "0"), ("15", "3")]
inputs | map((pair) => process(pair[0], pair[1]) or "ERROR")
