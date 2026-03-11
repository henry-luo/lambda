// Match Expression with String Pattern Arms
// Tests that match arms can use named string patterns for regex matching

'===== MATCH STRING PATTERN TESTS ====='

// ============================================================
// Test 1: Basic match with string pattern arms
// ============================================================
'Test 1: Basic Pattern Match'

type digits = \d+
type alpha = \a+

fn classify(s) => match s {
    case digits: "number"
    case alpha: "word"
    default: "other"
}

1; classify("123")            // "number"
2; classify("hello")          // "word"
3; classify("hello world")    // "other" (space doesn't match \a+)

// ============================================================
// Test 2: Match with whitespace pattern
// ============================================================
'Test 2: Whitespace Pattern'

type ws_only = \s+

fn check_whitespace(s) => match s {
    case ws_only: "yes"
    default: "no"
}

4; check_whitespace("   ")        // "yes"
5; check_whitespace("  hello  ")  // "no"
6; check_whitespace("x")         // "no" (not whitespace)

// ============================================================
// Test 3: Match with word patterns
// ============================================================
'Test 3: Word Patterns'

type word = \w+

fn is_word(s) => match s {
    case word: "yes"
    default: "no"
}

7; is_word("hello123")     // "yes"
8; is_word("hello world")  // "no" (contains space)
9; is_word("_test")        // "yes" (\w includes underscore)

// ============================================================
// Test 4: Match with multiple pattern arms and literal
// ============================================================
'Test 4: Mixed Pattern and Literal Arms'

type num = \d+
type name = \a+

fn tag(s) => match s {
    case "hello": "greeting"
    case num: "number"
    case name: "name"
    default: "unknown"
}

10; tag("hello")     // "greeting" (literal match first)
11; tag("42")        // "number"
12; tag("world")     // "name"
13; tag("hi there")  // "unknown"
