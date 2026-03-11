// Test: String Validation
// Layer: 4 | Category: integration | Covers: string patterns, is, match, find/replace

// ===== Define validation patterns =====
type email_pat = [a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}
type phone_pat = \d{3}-\d{3}-\d{4}
type zip_pat = ^\d{5}$
type alpha_pat = ^[a-zA-Z]+$
type num_pat = ^\d+$

// ===== Validate emails =====
"user@example.com" is email_pat
"invalid-email" is email_pat
"name@domain.org" is email_pat
"@missing.com" is email_pat

// ===== Validate phones =====
"555-123-4567" is phone_pat
"1234567890" is phone_pat
"555-abc-1234" is phone_pat

// ===== Validate zip codes =====
"12345" is zip_pat
"1234" is zip_pat
"123456" is zip_pat
"abcde" is zip_pat

// ===== Classify input =====
fn classify_input(s: string) => match s {
    case email_pat: "email"
    case phone_pat: "phone"
    case zip_pat: "zipcode"
    case num_pat: "number"
    case alpha_pat: "text"
    default: "unknown"
}
classify_input("user@test.com")
classify_input("555-123-4567")
classify_input("90210")
classify_input("42")
classify_input("hello")
classify_input("hello world 123")

// ===== Batch validation =====
let inputs = ["alice@test.com", "not-email", "bob@domain.org", "555-999-1234", "invalid"]
let valid_emails = inputs | filter((s) => s is email_pat)
valid_emails

// ===== String transformations =====
let text = "Hello World 123 Foo Bar"

// Find pattern
text | find(\d+)

// Replace pattern
text | replace(\d+, "NUM")

// Split on pattern
"one,two,,three,four" | split(",")

// ===== Combined pipeline =====
let raw_data = ["alice@test.com", "555-123-4567", "bob", "90210", "invalid email@", "hello"]
let classified = raw_data | map((s) => {
    input: s,
    type: classify_input(s)
})
classified | map((c) => c.input & " -> " & c.type)

// ===== Password strength =====
type has_upper = [A-Z]
type has_lower = [a-z]
type has_digit = \d
type has_special = [!@#$%^&*]

fn password_strength(pw: string) {
    let checks = [
        pw is has_upper,
        pw is has_lower,
        pw is has_digit,
        pw is has_special,
        len(pw) >= 8
    ]
    checks | filter((c) => c == true) | len()
}
password_strength("abc")
password_strength("Abc123")
password_strength("Abc123!@")
password_strength("Str0ng!Pass")
