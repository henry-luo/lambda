// Edge cases for string pattern integration with type patterns

// ==== BOUNDARY CASES ====

// Empty pattern definitions
string empty = ""
string opt_empty = ""?

// Single character patterns
string single = "a"
string single_range = "a" to "a"

// Very deep nesting of patterns
string d1 = "0" to "9"
string d2 = d1 d1
string d3 = d2 d2
string d4 = d3 d3  // effectively digit[16]

// ==== CONFLICTING DEFINITIONS ====

// Pattern shadowing type name (should this be allowed?)
// type email = string  // type definition
// string email = ...   // pattern definition

// ==== COMPLEX UNION/INTERSECTION WITH PATTERNS ====

// Union of multiple patterns
string numeric_format = digit+ | digit+ "." digit+ | "0x" ("0" to "9" | "a" to "f")+

// Pattern in complex type union
type ConfigValue = string | int | bool | date_iso

// Pattern with optional in map
type FlexibleContact = {
    email: email?,
    phone: phone_us?,
    fax: phone_us?
}

// ==== ERROR CASES (should fail gracefully) ====

// Invalid pattern syntax tests (for error handling)
// string bad1 = "a" to   // missing end
// string bad2 = [3]      // occurrence without base
// string bad3 = || "a"   // double alternation

// ==== DEEPLY NESTED SCHEMA WITH PATTERNS ====

type DeepSchema = {
    level1: {
        level2: {
            level3: {
                email: email,
                data: [{
                    id: identifier,
                    timestamp: date_iso
                }*]
            }
        }
    }
}

// ==== PATTERN IN ELEMENT TYPES ====

type EmailElement = <email; email>
type LinkElement = <a href: url; string>
type MetaElement = <meta name: identifier, content: string>

// Document with patterns
type Document = <doc version: semver;
    <head;
        <title; string>,
        <meta name: identifier, content: string;>*
    >,
    <body;
        <p; string | <a href: url; string>>*
    >
>

// ==== OCCURRENCE OPERATORS ON PATTERNS ====

// Pattern arrays with occurrences
type EmailArray = [email]
type EmailPlus = [email+]
type EmailStar = [email*]
type EmailOpt = [email?]

// Nested occurrences
type NestedEmails = [[email*]+]

// ==== PATTERN AS FUNCTION PARAMETER/RETURN ====

// Function with pattern parameter type
type EmailValidator = fn(email) bool
type EmailParser = fn(string) email?
type EmailFormatter = fn(email) string

// ==== TYPE ALIAS CHAIN WITH PATTERNS ====

string base_email = email_local "@" email_domain
string work_email = base_email  // alias
type WorkEmailField = work_email  // as type

// ==== MIXED STRING ENUM AND STRING PATTERN ====

// String enum (exact values)
type Status = "active" | "inactive" | "pending"

// String pattern (format constraint)  
string status_code = letter[3] digit[3]

// Combined in schema
type Record = {
    status: Status,           // must be exact value
    code: status_code,        // must match pattern
    email: email              // must match pattern
}

// ==== TESTING 'is' OPERATOR WITH PATTERNS IN TYPES ====

"test@example.com" is email
"123-456-7890" is phone_us
"active" is Status  // string enum check

// Complex is checks
let val = "john@company.com"
val is email
val is string  // pattern is subtype of string

// ==== NULL HANDLING WITH PATTERNS ====

type NullablePattern = {
    email: email?,
    phone: phone_us?
}

// null should be valid for optional pattern fields
// let nullable_test: NullablePattern = {email: null, phone: null}

// ==== EDGE CASE: PATTERN THAT MATCHES NOTHING ====

// Impossible pattern (if allowed)
// string impossible = "a" & "b"  // intersection that never matches

// ==== EDGE CASE: PATTERN THAT MATCHES EVERYTHING ====

string any_char = \.
string any_string = \.*  // or ...
