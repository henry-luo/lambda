// Test: String Patterns
// Layer: 2 | Category: statement | Covers: pattern literals, is matching, composition

// ===== Basic pattern literal =====
type digits = \d+
"12345" is digits
"hello" is digits

// ===== Letter pattern =====
type letters = [a-zA-Z]+
"hello" is letters
"123" is letters

// ===== Alphanumeric =====
type alnum = [a-zA-Z0-9]+
"abc123" is alnum
"!!!" is alnum

// ===== Email-like pattern =====
type email_pat = [a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}
"user@example.com" is email_pat
"not-an-email" is email_pat

// ===== Pattern with anchors =====
type starts_with_hello = ^Hello
"Hello World" is starts_with_hello
"Say Hello" is starts_with_hello

// ===== Pattern in match =====
type num_pat = ^\d+$
fn classify_string(s: string) => match s {
    case num_pat: "numeric"
    default: "non-numeric"
}
classify_string("42")
classify_string("hello")
classify_string("12abc")

// ===== Pattern with quantifiers =====
type word3 = ^[a-z]{3}$
"abc" is word3
"ab" is word3
"abcd" is word3

// ===== Pattern with alternation =====
type yes_no = ^(yes|no)$
"yes" is yes_no
"no" is yes_no
"maybe" is yes_no

// ===== Whitespace pattern =====
type has_space = \s
"hello world" is has_space
"helloworld" is has_space

// ===== Dot pattern =====
type three_chars = ^...$
"abc" is three_chars
"ab" is three_chars
"abcd" is three_chars

// ===== Used in filter =====
type upper_pat = ^[A-Z]+$
["HELLO", "world", "FOO", "bar", "BAZ"] | filter((s) => s is upper_pat)
