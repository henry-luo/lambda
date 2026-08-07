// Symbol patterns share content syntax with string patterns but retain a
// symbol-only domain at `is`/match boundaries.
'===== SYMBOL PATTERN TESTS ====='

type string_digits = \(d+)
type symbol_digits = \symbol(d+)
type symbol_digits_from_string = \symbol(string_digits)

'Test 1: Domain and content'
1; "123" is string_digits
2; '123' is string_digits
3; '123' is symbol_digits
4; "123" is symbol_digits
5; '123' is symbol_digits_from_string
6; 'abc' is symbol_digits

'Test 2: Symbol match arm'
fn classify(value) => match value {
    case symbol_digits: "symbol-digits"
    case string_digits: "string-digits"
    default: "other"
}

7; classify('456')
8; classify("456")
9; classify('abc')

'Test 3: Partial string operations reject symbol patterns'
10; find("x123", symbol_digits)
11; replace("x123", symbol_digits, "x")
12; split("x123", symbol_digits)
