# Lambda String Patterns

String and symbol patterns are first-class type values. They validate schemas,
select `match` arms, check values with `is`, and drive string `find`, `replace`
and `split` (S11.1.2v4, S17.6.1).

Related: [Types](Lambda_Type.md), [String Functions](Lambda_Sys_Func.md#string-functions),
[Validation](Lambda_Validator.md).

## Pattern Definition Syntax

Both domains use the same balanced delimiter. Quotes and named patterns establish
one consistent domain per island:

```lambda
type StringIdent = \("\a" "\w"*)
type SymbolIdent = \('\a' '\w'*)
"abc1" is StringIdent                 // true
'abc1' is SymbolIdent                 // true
"abc1" is SymbolIdent                 // false
fn width(s: \("\a"+)) => len(s)
let code: \("\a"{3}) = "abc"
```

The retired `\symbol(...)` spelling is rejected. The delimiter distinguishes
repetition within one text value from an ordinary type occurrence, which counts
separate values (S11.1.6v3):

```lambda
type Values = "a"*                    // null, "a", or a run of separate "a" values
type Text = \("a"*)                   // "", "a", "aa", ...
```

## Literal Patterns

Ordinary quoted characters are literal. A literal-only island is the same type
as its same-domain literal union: `\("hello" | "hi")` equals `"hello" | "hi"`;
`\('if' | 'else')` equals `'if' | 'else'`.

## Character Classes

Class escapes are recognized only inside an island:

| String atom | Symbol atom | Matches |
|---|---|---|
| `"\d"` | `'\d'` | digit `[0-9]` |
| `"\w"` | `'\w'` | word character `[a-zA-Z0-9_]` |
| `"\s"` | `'\s'` | whitespace `[\t\n\f\r ]` |
| `"\a"` | `'\a'` | alphabetic `[a-zA-Z]` |

Bare `d`, `w`, `s`, `a` are ordinary named-pattern references. Quoted fragments
can mix literal characters and classes. Only the four designated escapes
introduce classes; operators stay outside quotes. Ordinary escapes are decoded
once. Outside an island, the four new escapes are invalid.

```lambda
type Code = \("ID-\d" "\w"*)
type SymbolCode = \('ID-\d' '\w'*)
"1+" is \("\d+")                     // true: digit then literal plus
"12" is \("\d"+)                     // true: repeated digit
"\\d" is \("\\d")                    // true: literal backslash and d
"\n" is \("\n")                      // true: literal newline
```

## Character Ranges

A range is one atom; its bounds must be single Unicode code points:

```lambda
type Lower = \("a" to "z")
type SymbolLower = \('a' to 'z')
type HexDigit = \("0" to "9" | "a" to "f" | "A" to "F")
type Greek = \("α" to "ω")
```

Ordinary string range types may also be referenced in string patterns (S11.1.3).

## Occurrence Modifiers

| Quantifier | Meaning within an island |
|---|---|
| `?` | zero or one |
| `+` | one or more |
| `*` | zero or more |
| `{n}` | exactly n |
| `{n+}` | at least n |
| `{n,m}` | between n and m, inclusive |

A quantifier repeats the entire preceding atom. One quoted fragment is one atom,
including its embedded classes:

```lambda
"abab" is \("ab"+)                   // true
"a a " is \("a\s"*)                 // true
type Phone = \("\d"{3} "-" "\d"{3} "-" "\d"{4})
type Zip = \("\d"{5} ("-" "\d"{4})?)
```

Counts use braces; `{n,}` is rejected. The matching engine caps counts at 1000.

## Pattern Composition

Binding order, tightest first: atom (including a range), prefix `!`, quantifier,
concatenation, `|` (S11.1.2v4). Group with parentheses.

```lambda
"bc" is \("a" | "b" "c")             // true: a or bc
"ac" is \(("a" | "b") "c")           // true: grouping changes the order
"xy" is \(!"\d"+)                   // true: (!"\d")+
```

Every quoted fragment and named reference must have the same domain, even across
alternatives. `\("a" 'b')` and `\("a" | 'b')` are compile errors. A mixed-domain
union is written between whole islands: `\("\d"+) | \('\d'+)`.

The island's only binary operator is `|`. Intersect or exclude whole patterns
with the type operators (S10.1.1v3):

```lambda
type AlphaWord = \("\a"+) & \("\w"+)
type NonNumericWord = \("\w"+) ! \("\d"+)
```

## Character-Set Negation

Prefix `!` complements a single-character set in the island's domain: a class,
range, one-character literal, or a union, group, negation or named pattern built
from these. Negating a multi-character fragment or repetition is a compile error.

```lambda
type NotDigit = \(!"\d")
type SymbolNotDigit = \(!'\d')
type NotABC = \(!("a" | "b" | "c"))
type Tag = \("<" (!">")* ">")
```

Whole-type complement is written outside the island: `!\("\d"+)`.

## Wildcards

Bare `.` matches one code point except newline; `...` matches any sequence,
including newlines. Both inherit the domain established elsewhere in the island:

```lambda
type StringTail = \("prefix" ...)
type SymbolTail = \('prefix' ...)
type AnyString = \("" ...)
type StringPair = \("" . .)
```

An island with no domain-bearing literal or reference is a compile error,
including `\(.)`, `\(...)` and `\((.)*)`. There is no default string domain or
implicit dual-domain pattern. Quoted `"."` and `"..."` are literal punctuation.

## Complex Pattern Examples

```lambda
type Email = \("\w"+ "@" "\w"+ "." "\a"{2,6})
type Version = \("v" "\d"+ "." "\d"+ "." "\d"+)
type DatePattern = \("\d"{4} "-" "\d"{2} "-" "\d"{2})
type Username = \("\a" "\w"{2,19})
```

## Symbol Patterns

Single-quoted fragments and class escapes provide the same operations in the
symbol domain. Named references keep their domain:

```lambda
type SymbolDigits = \('\d'+)
type SymbolCode = \('ID-' SymbolDigits)
type Copy = \(SymbolDigits)
```

Implicit cross-domain reuse and the old `\symbol(StringPattern)` lift are
rejected. String and symbol values never satisfy each other's patterns.

## Using Patterns as Types

```lambda
type Digits = \("\d"+)
fn label(value: Digits) => "number:" ++ value
let code: Digits = "123"
"123" is Digits                      // true
```

## Pattern Matching with `match`

`is` and `match` require the whole text to match; `match` chooses the first
matching arm (S11.2.1):

```lambda
fn classify(value) => match value {
    case \("\d"+): "string digits"
    case \('\d'+): "symbol digits"
    default: "other"
}
```

## Pattern-Aware String Functions

`find`, `replace` and `split` search within a string and require string-domain
patterns; symbol patterns produce a domain error.

```lambda
type Digits = \("\d"+)
find("a1b22", Digits)                 // [{value: "1", index: 1}, {value: "22", index: 3}]
replace("a1b22", Digits, "N")         // "aNbN"
split("a1b22c", Digits)               // ["a", "b", "c"]
split("a1b22c", Digits, true)         // ["a", "1", "b", "22", "c"]
```

Replacement text is literal; empty-matching patterns yield empty matches too
(S17.6.1). Search indices count Unicode code points (S17.4.1).
