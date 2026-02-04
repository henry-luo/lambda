// Test: String Pattern Integration with Type Pattern
// Tests using string patterns as field types in type patterns

// Define string patterns for validation using Lambda syntax
string phone = \d[3] "-" \d[3] "-" \d[4]
string email_simple = \w+ "@" \w+ "." \w+
string zip = \d[5]
string identifier = ("a" to "z" | "A" to "Z" | "_") ("a" to "z" | "A" to "Z" | "0" to "9" | "_")*

// ============================================================
// Test 1: Basic pattern matching - positive cases
// ============================================================
"=== Test 1: Basic Pattern Matching (Positive) ==="

let test_email = "user@example.com"
let test_phone = "123-456-7890"
let test_zip = "12345"
let test_id = "my_var_123"

"1.1 - phone match"; (test_phone is phone)
"1.2 - email match"; (test_email is email_simple)
"1.3 - zip match"; (test_zip is zip)
"1.4 - identifier match"; (test_id is identifier)

// ============================================================
// Test 2: Pattern matching - negative cases
// ============================================================
"=== Test 2: Pattern Matching (Negative) ==="

let bad_phone1 = "1234567890"      // missing dashes
let bad_phone2 = "12-456-7890"    // wrong digit count
let bad_phone3 = "123-45-7890"    // wrong middle section
let bad_phone4 = "abc-def-ghij"   // letters instead of digits
// Note: Empty string "" is null in Lambda, so we test with single space
let bad_phone5 = " "              // single space

"2.1 - phone no dashes"; (not (bad_phone1 is phone))
"2.2 - phone wrong first"; (not (bad_phone2 is phone))
"2.3 - phone wrong middle"; (not (bad_phone3 is phone))
"2.4 - phone letters"; (not (bad_phone4 is phone))
"2.5 - phone space"; (not (bad_phone5 is phone))

let bad_email1 = "not-an-email"   // no @ symbol
let bad_email2 = "@example.com"   // no local part
let bad_email3 = "user@"          // no domain
let bad_email4 = " "              // single space

"2.6 - email no at"; (not (bad_email1 is email_simple))
"2.7 - email no local"; (not (bad_email2 is email_simple))
"2.8 - email no domain"; (not (bad_email3 is email_simple))
"2.9 - email space"; (not (bad_email4 is email_simple))

let bad_zip1 = "1234"             // too short
let bad_zip2 = "123456"           // too long
let bad_zip3 = "abcde"            // letters

"2.10 - zip too short"; (not (bad_zip1 is zip))
"2.11 - zip too long"; (not (bad_zip2 is zip))
"2.12 - zip letters"; (not (bad_zip3 is zip))

let bad_id1 = "123abc"            // starts with digit
let bad_id2 = "-invalid"          // starts with hyphen

"2.13 - id starts digit"; (not (bad_id1 is identifier))
"2.14 - id starts hyphen"; (not (bad_id2 is identifier))

// ============================================================
// Test 3: Type pattern with required string pattern field
// ============================================================
"=== Test 3: Type with Required Pattern Field ==="

type PhoneRecord = {
  label: string,
  phone_num: phone
}

let valid_phone_rec = { label: "Home", phone_num: "123-456-7890" }
"3.1 - valid phone record"; (valid_phone_rec is PhoneRecord)

// ============================================================
// Test 4: Type pattern with optional string pattern field
// ============================================================
"=== Test 4: Type with Optional Pattern Field ==="

type Contact = {
  name: string,
  phone: phone?,
  email: email_simple?
}

let contact_full = {
  name: "Alice",
  phone: "123-456-7890",
  email: "alice@test.com"
}
"4.1 - contact with all fields"; (contact_full is Contact)

let contact_phone_only = {
  name: "Bob",
  phone: "987-654-3210"
}
"4.2 - contact phone only"; (contact_phone_only is Contact)

let contact_email_only = {
  name: "Carol",
  email: "carol@test.com"
}
"4.3 - contact email only"; (contact_email_only is Contact)

let contact_minimal = {
  name: "Dave"
}
"4.4 - contact name only"; (contact_minimal is Contact)

// ============================================================
// Test 5: Edge cases - boundary values
// ============================================================
"=== Test 5: Edge Cases ==="

// Exact boundary for phone pattern
let phone_exact = "000-000-0000"
"5.1 - phone all zeros"; (phone_exact is phone)

let phone_nines = "999-999-9999"
"5.2 - phone all nines"; (phone_nines is phone)

// Single character edge cases for identifier
let id_single_letter = "a"
let id_single_underscore = "_"
"5.3 - id single letter"; (id_single_letter is identifier)
"5.4 - id single underscore"; (id_single_underscore is identifier)

// Long identifier
let id_long = "this_is_a_very_long_identifier_name_123"
"5.5 - id long"; (id_long is identifier)

// ============================================================
// Test 6: Multiple pattern fields in same type
// ============================================================
"=== Test 6: Multiple Pattern Fields ==="

type UserProfile = {
  username: identifier,
  email: email_simple,
  phone: phone?,
  zip: zip?
}

let full_profile = {
  username: "john_doe",
  email: "john@example.com",
  phone: "555-123-4567",
  zip: "90210"
}
"6.1 - full profile"; (full_profile is UserProfile)

let minimal_profile = {
  username: "jane",
  email: "jane@test.org"
}
"6.2 - minimal profile"; (minimal_profile is UserProfile)

// ============================================================
// Test 7: Nested types with patterns
// ============================================================
"=== Test 7: Nested Types with Patterns ==="

type Address = {
  street: string,
  city: string,
  zip: zip
}

type Person = {
  name: string,
  email: email_simple,
  address: Address?
}

let person_with_addr = {
  name: "Eve",
  email: "eve@mail.com",
  address: {
    street: "123 Main St",
    city: "Springfield",
    zip: "12345"
  }
}
"7.1 - person with address"; (person_with_addr is Person)

let person_no_addr = {
  name: "Frank",
  email: "frank@mail.com"
}
"7.2 - person without address"; (person_no_addr is Person)

"=== All string pattern integration tests completed! ==="
