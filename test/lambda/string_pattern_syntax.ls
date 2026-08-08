// Syntax-focused coverage for the delimited pattern island.
'===== STRING PATTERN SYNTAX TESTS ====='

type literal_union = \("a" | "b")
type bare_literal_union = "a" | "b"
type nested = \(("a" | "b")+)
type escaped = \("a\n" | "b\n")
type parens = \("(" ")")
type wild = \(...)
type not_digit = \(!d)
type ascii_range = \("a" to "z")
type first_digit = \(d+), second_word = \(w+)

'Test 1: Literal and structural islands'
1; "a" is literal_union
2; "c" is literal_union
3; "abab" is nested
4; "a\n" is escaped
5; "()" is parens
6; "anything" is wild
7; "a" is not_digit
8; "5" is not_digit
9; "m" is ascii_range

'Test 2: Literal-union equivalence'
10; "a" is bare_literal_union
11; "a" is literal_union
12; "b" is bare_literal_union
13; "b" is literal_union

'Test 3: Inline and multi-declare islands'
14; "123" is \(d+)
15; "hello" is \(w+)
16; "123" is first_digit
17; "hello" is second_word

'Test 4: Range overload remains available'
18; 95 is (90 to 100)
19; 101 is (90 to 100)
type Byte = 0 to 255
let x: "a" to "z" = "m"
20; x is ("a" to "z")
21; 95 is Byte
22; 256 is Byte
