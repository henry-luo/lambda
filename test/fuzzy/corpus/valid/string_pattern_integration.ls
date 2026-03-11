// String pattern integration with type patterns - tests pattern as type in various contexts

// ==== BASIC STRING PATTERN DEFINITIONS ====
type digit = "0" to "9"
type letter = "a" to "z" | "A" to "Z"
type alphanumeric = letter | digit
type identifier = (letter | "_") (alphanumeric | "_")*

// Common format patterns
type email_local = (alphanumeric | "." | "_" | "-")+
type email_domain = alphanumeric+ ("." alphanumeric+)+
type email = email_local "@" email_domain

type phone_us = digit[3] "-" digit[3] "-" digit[4]
type zip_code = digit[5] ("-" digit[4])?
type date_iso = digit[4] "-" digit[2] "-" digit[2]
type time_24h = digit[2] ":" digit[2] ":" digit[2]

type url_scheme = letter+
type url = url_scheme "://" alphanumeric+ ("." alphanumeric+)* ("/" alphanumeric*)*

type semver = digit+ "." digit+ "." digit+
type uuid = digit[8] "-" digit[4] "-" digit[4] "-" digit[4] "-" digit[12]

// ==== STRING PATTERNS AS FIELD TYPES IN MAPS ====
type Person = {
    name: string,
    email: email,
    phone: phone_us?
}

type Address = {
    street: string,
    city: string,
    state: string,
    zip: zip_code
}

type UserProfile = {
    username: identifier,
    email: email,
    created: date_iso,
    avatar_url: url?
}

// ==== STRING PATTERNS IN ARRAYS ====
type EmailList = [email*]
type PhoneBook = [{name: string, phone: phone_us}+]

// ==== STRING PATTERNS WITH OPTIONALS ====
type OptionalContact = {
    primary_email: email,
    secondary_email: email?,
    work_phone: phone_us?,
    home_phone: phone_us?
}

// ==== STRING PATTERNS IN UNION TYPES ====
type ContactMethod = email | phone_us
type Identifier = identifier | uuid

// ==== STRING PATTERNS IN NESTED STRUCTURES ====
type Company = {
    name: string,
    website: url,
    contacts: [{
        name: string,
        email: email,
        role: string
    }*],
    headquarters: Address
}

// ==== SYMBOL PATTERNS (if supported) ====
// symbol snake_case = ('a to 'z) ('a to 'z | '0 to '9 | '_)*
// symbol camelCase = ('a to 'z) ('a to 'z | 'A to 'Z | '0 to '9)*

// ==== TESTING PATTERN IS OPERATOR ====
"john@example.com" is email
"555-123-4567" is phone_us
"12345" is zip_code
"12345-6789" is zip_code
"2026-02-04" is date_iso
"https://example.com/path" is url

// Invalid patterns should fail
// "not-an-email" is email  // false
// "123-456" is phone_us    // false

// ==== COMPLEX INTEGRATION TEST ====
type APIResponse = {
    status: "success" | "error",
    version: semver,
    timestamp: date_iso,
    data: {
        users: [{
            id: uuid,
            email: email,
            created: date_iso
        }*]
    }?
}

// Test instantiation (if validation is implemented)
let test_person = {
    name: "John Doe",
    email: "john@example.com",
    phone: "555-123-4567"
}

let test_profile = {
    username: "johndoe",
    email: "john@example.com",
    created: "2026-02-04",
    avatar_url: "https://example.com/avatar.png"
}

// ==== EDGE CASES ====
// Empty string pattern (boundary)
type empty_or_digit = "" | digit+

// Very long pattern chain
type hex_char = "0" to "9" | "a" to "f" | "A" to "F"
type hex_string = "0x" hex_char+
type hex_color = "#" hex_char[6] | "#" hex_char[3]

// Pattern with special characters
type quoted_string = '"' (letter | digit | " " | "!" | "?" | "." | ",")* '"'

// Recursive-like patterns (reference other patterns)
type nested_parens_simple = "(" alphanumeric* ")"
