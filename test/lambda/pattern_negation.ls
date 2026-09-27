// S11.1.2v3: `!` inside a pattern island complements a single-character set:
// a class, a range, a one-character string, a negated set, or a union, group
// or named pattern built only from these. It lowers to one `[^…]`-style class.

'1. named classes'
"1.1"; ("x" is \(!d))
"1.2"; ("5" is \(!d))
"1.3"; ("-" is \(!w))
"1.4"; ("\t" is \(!s))

'2. a union of one-character strings'
"2.1"; ("x" is \(!("a" | "b" | "c")))
"2.2"; ("a" is \(!("a" | "b" | "c")))
"2.3"; ("" is \(!("a" | "b" | "c")))
"2.4"; ("xy" is \(!("a" | "b" | "c")))
"2.5"; ("y" is \(!"x"))
"2.6"; ("x" is \(!"x"))

'3. ranges, groups, nesting'
"3.1"; ("1" is \(!("a" to "z")))
"3.2"; ("m" is \(!("a" to "z")))
"3.3"; ("11" is \("1" !("a" to "z")))
"3.4"; ("x" is \(!(d)))
"3.5"; ("5" is \(!(!d)))
"3.6"; ("x" is \(!(!d)))
"3.7"; ("_" is \(!(d | "-" | "a" to "f")))
"3.8"; ("b" is \(!(d | "-" | "a" to "f")))

'4. named patterns, literal unions and character range types'
type Digit = \(d)
type Vowel = "a" | "e" | "i" | "o" | "u"
type Lower = "a" to "z"
type Hex = "0" to "9" | "a" to "f"
type Tail = "ab" | "x" to "z"
"4.1"; ("x" is \(!Digit))
"4.2"; ("5" is \(!Digit))
"4.3"; ("b" is \(!Vowel))
"4.4"; ("e" is \(!Vowel))
"4.5"; ("aeiou" is \(Vowel+))
"4.6"; ("qq" is \(Lower+))
"4.7"; ("qQ" is \(Lower+))
"4.8"; ("Q" is \(!Lower))
"4.9"; ("q" is \(!Lower))
"4.10"; ("c0ffee" is \(Hex+))
"4.11"; ("g" is \(!Hex))
"4.12"; ("abyz" is \(Tail+))
"4.13"; ("abc" is \(Tail+))

'5. code points beyond ASCII'
"5.1"; ("β" is \("α" to "ω"))
"5.2"; ("b" is \("α" to "ω"))
"5.3"; ("b" is \(!("α" to "ω")))
"5.4"; ("β" is \(!("α" to "ω")))
"5.5"; ("日" is \(!("a" | "b")))

'6. searching and the symbol domain'
"6.1"; (find("<a><b>", \("<" (!">")* ">")) |> ~.value)
"6.2"; split("a1b22c", \(!("a" to "z")+))
"6.3"; ('x' is \symbol(!("a" | "b")))
"6.4"; ('a' is \symbol(!("a" | "b")))
"6.5"; (find("a1b2", Lower) |> ~.value)
"6.6"; ('qq' is \symbol(Lower+))

'7. no binary `!` inside a pattern (SP18); exclusion is between whole patterns'
"7.1"; ("ab" is \(w ! d))
"7.2"; ("a" is \(w ! d))
"7.3"; ("ab" is (\(w+) ! \(d+)))
"7.4"; ("12" is (\(w+) ! \(d+)))
