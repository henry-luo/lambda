// S11.1.2v3: a pattern is a type value, usable wherever a type is. As a
// declared type it admits text of its tag's domain that matches it; `let` and
// parameter annotations had read it as the type of type values (E201, E207).
// A pattern value prints as its canonical source (printing one crashed), and
// `...` is any string, newlines included (S16.8.6v3).

type Code = \("\a"{3})
type Digits = \("\d"+)
type Ident = \('\a' '\w'*)

fn shout(s: Digits) => s ++ "!"
fn width(s: \("\a"+)) => len(s)
fn sym(s: Ident) => s
fn pass(s: Digits) => s
fn admit(s) { let code: Code = s; code }

'1. declarations and parameters'
let code: \("\a"{3}) = "abc"
let named: Code = "xyz"
"1.1"; [code]
"1.2"; [named]
"1.3"; [shout("42")]
"1.4"; [width("abcd")]
"1.5"; [sym('ab1')]
"1.6"; [shout(pass("7"))]
"1.7"; [admit("abc")]
"1.8"; [admit("ab1")]

'2. a pattern as a type value'
let t: type = \("\d"+)
let u: type = Digits
"2.1"; ("12" is t)
"2.2"; ("12" is u)
"2.3"; [\("\d"+), Digits, \('\a' '\w'*)]

'3. `...` is any string'
"3.1"; ("a\nb" is \("" ...))
"3.2"; ("<a\nb>" is \("<" ... ">"))

'4. counts take the type spellings, blanks allowed'
"4.1"; ("12345" is \("\d"{2, 5}))
"4.2"; ("123456" is \("\d"{2, 5}))
"4.3"; ("11" is \("\d"{ 2+ }))
