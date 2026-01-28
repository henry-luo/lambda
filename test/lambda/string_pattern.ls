// String Pattern Feature Test Suite
// Tests the string pattern definition and matching functionality

"===== STRING PATTERN FEATURE TESTS ====="

// ============================================================
// Test 1: Simple Literal Patterns
// ============================================================
'Test 1: Simple Literal Patterns'

string hello_pattern = "hello"
"1.1"; ("hello" is hello_pattern)    // true - exact match
"1.2"; ("world" is hello_pattern)    // false - different string
"1.3"; ("HELLO" is hello_pattern)    // false - case sensitive
"1.4"; ("hello!" is hello_pattern)   // false - extra character

// ============================================================
// Test 2: Character Class \d (digits)
// ============================================================
'Test 2: Digit Character Class (\\d)'

string single_digit = \d
string multi_digit = \d+
string exact_3_digits = \d{3}

"2.1"; ("5" is single_digit)         // true
"2.2"; ("a" is single_digit)         // false
"2.3"; ("12345" is multi_digit)      // true
"2.4"; ("abc" is multi_digit)        // false
"2.5"; ("123" is exact_3_digits)     // true
"2.6"; ("12" is exact_3_digits)      // false - too few
"2.7"; ("1234" is exact_3_digits)    // false - too many

// ============================================================
// Test 3: Character Class \w (word characters)
// ============================================================
'Test 3: Word Character Class (\\w)'

string word_chars = \w+

"3.1"; ("hello" is word_chars)       // true
"3.2"; ("hello123" is word_chars)    // true - alphanumeric
"3.3"; ("hello_world" is word_chars) // true - underscore is word char
"3.4"; ("hello world" is word_chars) // false - space not word char
"3.5"; ("hello-world" is word_chars) // false - hyphen not word char

// ============================================================
// Test 4: Character Class \s (whitespace)
// ============================================================
'Test 4: Whitespace Character Class (\\s)'

string whitespace = \s+

"4.1"; (" " is whitespace)           // true - space
"4.2"; ("   " is whitespace)         // true - multiple spaces
"4.3"; ("a" is whitespace)           // false - not whitespace

// ============================================================
// Test 5: Any Character (.)
// ============================================================
'Test 5: Any Character (.)'

string any_char = .
string any_three = ...

"5.1"; ("x" is any_char)             // true
"5.2"; ("abc" is any_three)          // true
"5.3"; ("ab" is any_three)           // false - only 2 chars

// ============================================================
// Test 6: Pattern Concatenation (Sequences)
// ============================================================
'Test 6: Pattern Concatenation'

string phone = \d{3} "-" \d{3} "-" \d{4}
string email_simple = \w+ "@" \w+ "." \w+

"6.1"; ("123-456-7890" is phone)     // true
"6.2"; ("1234567890" is phone)       // false - no dashes
"6.3"; ("12-456-7890" is phone)      // false - wrong digit count
"6.4"; ("user@domain.com" is email_simple)  // true
"6.5"; ("invalid" is email_simple)   // false - no @ symbol

// ============================================================
// Test 7: Optional Pattern (?)
// ============================================================
'Test 7: Optional Pattern (?)'

string optional_dash = \d{3} "-"? \d{4}

"7.1"; ("123-4567" is optional_dash) // true - with dash
"7.2"; ("1234567" is optional_dash)  // true - without dash
"7.3"; ("123--4567" is optional_dash) // false - double dash

// ============================================================
// Test 8: One or More (+)
// ============================================================
'Test 8: One or More (+)'

string one_or_more_digits = \d+

"8.1"; ("1" is one_or_more_digits)   // true - one digit
"8.2"; ("12345" is one_or_more_digits) // true - many digits
// Note: empty string is typed as null in Lambda, so pattern match returns error

// ============================================================
// Test 9: Zero or More (*)
// ============================================================
'Test 9: Zero or More (*)'

string zero_or_more_a = "a"*

"9.1"; ("aaa" is zero_or_more_a)     // true - multiple a's
"9.2"; ("a" is zero_or_more_a)       // true - one a
// Note: empty string test skipped (typed as null)

// ============================================================
// Test 10: Exact Count {n}
// ============================================================
'Test 10: Exact Count {n}'

string exactly_5 = \d{5}

"10.1"; ("12345" is exactly_5)       // true
"10.2"; ("1234" is exactly_5)        // false - 4 digits
"10.3"; ("123456" is exactly_5)      // false - 6 digits

// ============================================================
// Test 11: Range Count {n,m}
// ============================================================
'Test 11: Range Count {n,m}'

string range_2_4 = \d{2,4}

"11.1"; ("12" is range_2_4)          // true - 2 digits
"11.2"; ("123" is range_2_4)         // true - 3 digits
"11.3"; ("1234" is range_2_4)        // true - 4 digits
"11.4"; ("1" is range_2_4)           // false - 1 digit
"11.5"; ("12345" is range_2_4)       // false - 5 digits

// ============================================================
// Test 12: Character Range ("a" to "z")
// ============================================================
'Test 12: Character Range'

string lowercase = ("a" to "z")+
string uppercase = ("A" to "Z")+
string digit_range = ("0" to "9")+

"12.1"; ("hello" is lowercase)       // true
"12.2"; ("HELLO" is lowercase)       // false
"12.3"; ("HELLO" is uppercase)       // true
"12.4"; ("hello" is uppercase)       // false
"12.5"; ("12345" is digit_range)     // true

// ============================================================
// Test 13: Union Pattern (|)
// ============================================================
'Test 13: Union Pattern (|)'

string yes_or_no = "yes" | "no"
string digit_or_letter = \d | \w

"13.1"; ("yes" is yes_or_no)         // true
"13.2"; ("no" is yes_or_no)          // true
"13.3"; ("maybe" is yes_or_no)       // false
"13.4"; ("5" is digit_or_letter)     // true
"13.5"; ("a" is digit_or_letter)     // true

// ============================================================
// Test 14: Complex Combined Patterns
// ============================================================
'Test 14: Complex Combined Patterns'

string us_zip = \d{5} ("-" \d{4})?
string identifier = ("a" to "z" | "A" to "Z" | "_") \w*

"14.1"; ("12345" is us_zip)          // true - 5 digit zip
"14.2"; ("12345-6789" is us_zip)     // true - zip+4
"14.3"; ("1234" is us_zip)           // false - only 4 digits
"14.4"; ("myVar" is identifier)      // true
"14.5"; ("_private" is identifier)   // true
"14.6"; ("123abc" is identifier)     // false - starts with digit

// ============================================================
// Test 15: Alphabetic Pattern (\a = letters only)
// ============================================================
'Test 15: Alphabetic Pattern (\\a = letters only)'

string alphabetic = \a+

"15.1"; ("abc" is alphabetic)        // true - lowercase letters
"15.2"; ("ABC" is alphabetic)        // true - uppercase letters
"15.3"; ("abc123" is alphabetic)     // false - digits not matched
"15.4"; ("abc_def" is alphabetic)    // false - underscore not matched

// ============================================================
// Final Result
// ============================================================
"===== STRING PATTERN TESTS COMPLETED ====="
