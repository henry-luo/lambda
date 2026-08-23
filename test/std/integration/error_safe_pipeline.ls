// Test: Error Safe Pipeline
// Layer: 4 | Category: integration | Covers: error handling, T^E, braced handlers, or default

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
let r1 = process("abc", "5") ^ { ^ }
r1.message
r1.code

// ===== Error at divide stage =====
let r2 = process("10", "0") ^ { ^ }
r2.message
r2.code

// ===== Error at validate stage =====
let r3 = process("-10", "1") ^ { ^ }
// -10 / 1 = -10, which is not positive
r3.message

// ===== Or default pattern =====
let safe1 = process("10", "2") or "default"
safe1

let safe2 = process("abc", "1") or "fallback"
safe2;

// ===== expr is error check =====
(process("10", "2") ^ { ^ }) is error;
(process("abc", "1") ^ { ^ }) is error

// ===== Error chain =====
fn wrapped_process(a: string, b: string) string^ {
    let result = process(a, b) ^ { ^ }
    if (result) raise error("Process failed", source: result)
    result
}
let wr = wrapped_process("abc", "1") ^ { ^ }
wr.message
wr.source.message

// ===== Batch processing with error handling =====
let inputs = [("10", "2"), ("abc", "1"), ("20", "0"), ("15", "3")]
inputs |> map((pair) => process(pair[0], pair[1]) or "ERROR")
