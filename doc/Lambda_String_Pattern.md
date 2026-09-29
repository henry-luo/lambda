# Lambda String Patterns

String patterns are Lambda's regular-expression replacement, built into the type system. A pattern is a type value, so the same pattern validates a field in a schema, selects a `match` arm, checks a value with `is`, and drives `find`, `replace` and `split` (S11.1.2v3, S17.6.1).

> **Related Documentation**:
> - [Lambda Type System](Lambda_Type.md) — types, unions, constrained types
> - [Lambda System Functions](Lambda_Sys_Func.md#string-functions) — `find`, `replace`, `split` and the other string functions
> - [Lambda Validator Guide](Lambda_Validator.md) — patterns in schemas

---

## Table of Contents

1. [Pattern Definition Syntax](#pattern-definition-syntax)
2. [Literal Patterns](#literal-patterns)
3. [Character Classes](#character-classes)
4. [Character Ranges](#character-ranges)
5. [Occurrence Modifiers](#occurrence-modifiers)
6. [Pattern Composition](#pattern-composition)
7. [Character-Set Negation](#character-set-negation)
8. [Complex Pattern Examples](#complex-pattern-examples)
9. [Symbol Patterns](#symbol-patterns)
10. [Using Patterns as Types](#using-patterns-as-types)
11. [Pattern Matching with `match`](#pattern-matching-with-match)
12. [Pattern-Aware String Functions](#pattern-aware-string-functions)

---

String patterns are delimited type values. `\(...)` creates a string-domain pattern; `\symbol(...)` creates a symbol-domain pattern. The delimiter carries the domain, while the interior describes content. Full-match operations (`is` and `match`) check both domain and content.

## Pattern Definition Syntax

```lambda no-run
// Structural string pattern
type PatternName = \(pattern_expression)

// Structural symbol pattern
type SymbolPatternName = \symbol(pattern_expression)
```

Literal-only patterns do not need a delimiter: `type Greeting = "hi" | "hey"` is the same type representation as `type Greeting = \("hi" | "hey")`.

### Inline Patterns

A pattern is an ordinary type value, so it does not have to be named. The delimited form can appear anywhere a type can — in `is` checks, annotations, and `match` arms:

```lambda
"abc" is \(a+)                       // true
'foo' is \symbol(a w*)               // true

fn f(x: \(d+)) => "got " ++ x        // parameter annotation
let code: \(a{3}) = "abc"            // let annotation

match s {
    case \(d+): "number"             // inline arm
    default: "other"
}
```

Name a pattern when it is reused or when the name documents intent; inline it when it is used once.

## Literal Patterns

```lambda
// Exact string match
type Hello = \("hello")

// Alternatives with union operator
type Greeting = "hello" | "hi" | "hey"

// HTTP methods
type HttpMethod = "GET" | "POST" | "PUT" | "DELETE" | "PATCH"
```

## Character Classes

| Class | Matches |
|-------|---------|
| `d` | digit `[0-9]` |
| `w` | word character `[a-zA-Z0-9_]` |
| `s` | whitespace |
| `a` | alphabetic `[a-zA-Z]` |
| `.` | any single character |
| `...` | any characters (zero or more) |

A character class is only meaningful inside a pattern island `\(…)`:

```lambda
type Digit = \(d)                    // single digit
type Word = \(w+)                    // one or more word characters
type Anything = \(...)                // any string
```

### Reserved Class Names

The letters `d`, `w`, `s`, and `a` are **reserved as character classes inside `\(...)` only**. Outside a pattern island they are ordinary identifiers, so a type or variable may still be called `d`; it simply cannot be referenced from inside a pattern:

```lambda error=E200
type d = "binding"       // fine — ordinary type alias
type bad = \(d)          // error: pattern class 'd' is reserved inside
                         // pattern islands; rename the surrounding binding
```

To match one of these letters **literally**, quote it — quoted text inside a pattern is always literal content:

```lambda
type DVar = \("d" w+)    // matches "dx", "d_1" — a literal 'd', then word chars
type Digits = \(d+)      // matches "42" — the digit class
```

## Character Ranges

```lambda
// Range with 'to' keyword (like regex [a-z])
type LowerLetter = \("a" to "z")
type UpperLetter = \("A" to "Z")
type HexDigit = \("0" to "9" | "a" to "f" | "A" to "F")
type GreekLower = \("α" to "ω")      // bounds are any single characters
```

A range type named outside a pattern is the same set inside one:

```lambda
type Lower = "a" to "z"
"abc" is \(Lower+)                   // true
"Abc" is \(!Lower w+)                // true: one non-lowercase, then word chars
```

## Occurrence Modifiers

| Quantifier | Meaning |
|------------|---------|
| `?` | zero or one (optional) |
| `+` | one or more |
| `*` | zero or more |
| `{n}` | exactly n occurrences |
| `{n+}` | n or more occurrences |
| `{n,m}` | between n and m occurrences (inclusive) |

Counts are written as for [type occurrences](Lambda_Type.md#type-occurrence-modifiers). The open count is `{n+}`, not regex's `{n,}`.

A quantifier is a pattern fragment, not a type on its own; it attaches to the element it repeats. A quoted string is one element, so `\("ab"+)` matches `"abab"`, where regex `ab+` would match `"abb"`:

```lambda
type OptionalPrefix = \("pre"? w+)           // optional "pre" prefix
type Identifier = \(a w*)                    // letter followed by word chars
type ThreeDigits = \(d{3})                    // exactly 3 digits
type Phone = \(d{3} "-" d{3} "-" d{4})      // 555-123-4567
type ZipCode = \(d{5} ("-" d{4})?)           // 12345 or 12345-6789
```

## Pattern Composition

```lambda
// Sequence: patterns concatenate
type FullName = \(a+ " " a+)                 // first space last

// Union: match either pattern
type YesNo = "yes" | "no"

// Negation: one character outside a set
type NotDigit = \(!d)                         // any non-digit character
type Tag = \("<" (!">")* ">")                 // "<a>": regex <[^>]*>
```

Inside a pattern, the parts bind in this order, tightest first: an atom (a range `"a" to "z"` is one atom), a prefix `!`, a quantifier, concatenation, and `|` (S11.1.2v3). Group with parentheses to change it:

```lambda
"xy" is \(!d+)                  // true: (!d)+, one or more non-digits
"abb" is \("a" "b"+)            // true: the quantifier repeats only "b"
"bc" is \("a" | "b" "c")        // true: "a", or "b" followed by "c"
"ac" is \(("a" | "b") "c")      // true: the group changes the order
```

`|` is the only operator that joins two patterns. There is no `&` inside a pattern, and `!` there is only a prefix. To intersect or exclude, combine whole patterns with the type operators (S10.1.1v3): `"abc" is (\(a+) & \(w+))` is `true`, and `\(w+) ! \(d+)` admits word strings that are not all digits.

## Character-Set Negation

In string patterns, `!` negates a set of single characters and matches one character outside it, like regex `[^…]` (S11.1.2v3). The set can be a class, a range, a one-character string, or a union of these:

```lambda
// Any character except a digit
type NotDigit = \(!d)

// Any character except whitespace
type NotSpace = \(!s)

// Any character except a, b or c: regex [^abc]
type NotABC = \(!("a" | "b" | "c"))

// Any character except a closing angle bracket: regex [^>]
type NotClose = \(!">")
```

Negating anything longer than one character, such as `\(!"ab")` or `\(!(d+))`, is a compile error. To exclude a whole pattern, apply `!` to the pattern as a type: `"abc" is !\(d+)` is `true`.

Inside a pattern `!` is only a prefix; there is no binary `!`. Since whitespace joins the parts of a pattern, `\(w ! d)` is a word character followed by a non-digit. To exclude one pattern from another, put the exclusion between whole patterns: `\(w+) ! \(d+)` admits word strings that are not all digits.

## Complex Pattern Examples

```lambda
// Email-like pattern
type Email = \(w+ "@" w+ "." a{2,6})

// URL path segment
type PathSegment = \(("/" w+)+)

// Version string: v1.2.3
type Version = \("v" d+ "." d+ "." d+)

// Hex color: #RGB or #RRGGBB
type HexDigit = \("0" to "9" | "a" to "f" | "A" to "F")
type HexColor = \("#" (HexDigit{3} | HexDigit{6}))

// Date format: YYYY-MM-DD
type DatePattern = \(d{4} "-" d{2} "-" d{2})

// Username: 3-20 chars, starts with letter
type Username = \(a w{2,19})
```

## Symbol Patterns

Symbol patterns use the same content language but match symbols only:

```lambda
type Keyword = 'if' | 'else' | 'for' | 'while'  // literal symbol union
type SymbolIdentifier = \symbol(a w*)
type SymbolDigits = \symbol(d+)
```

**The tag selects the domain; the interior only describes content.** Matching checks the domain first, so a string never satisfies a symbol pattern and a symbol never satisfies a string pattern:

```lambda
type Ident = \(a w*)                  // string domain
type SymIdent = \symbol(a w*)         // symbol domain

'foo' is SymIdent                     // true
"foo" is SymIdent                     // false — string value, symbol pattern
'foo' is Ident                        // false — symbol value, string pattern
```

Because the interior is domain-free, a named pattern can be **reused as content in either domain** — define the shape once:

```lambda
type Ident = \(a w*)
type SymIdent = \symbol(Ident)        // same shape, symbol domain
```

Quoted literals inside a pattern body are always **string content**, whatever the tag. A symbol literal in a pattern body is an error, since the domain belongs to the tag:

```lambda error=E103
type bad = \('abc')      // error: pattern bodies are content-only; use
                         // \symbol(...) for the symbol domain and string
                         // literals for content
type good = \symbol("abc")   // the symbol 'abc'
```

Symbol enumerations need no pattern at all — a bare literal union is the idiomatic spelling, as with `Keyword` above.

`find`, `replace`, and `split` operate on strings, so passing a symbol-domain pattern to them is a domain error.

## Using Patterns as Types

Pattern names can be used as types for validation:

```lambda
// Use pattern as parameter type
fn validate_email(email: Email) => ...

// Use in type annotations
let method: HttpMethod = "GET"

// Type checking with 'is' (full-match semantics)
"hello" is Greeting              // true
"goodbye" is Greeting            // false
"v1.2.3" is Version              // true
```

## Pattern Matching with `match`

Named string patterns can be used as `match` arms. Each arm uses **full-match** semantics — the entire string must match the pattern:

```lambda
type digits = \(d+)
type alpha = \(a+)

fn classify(s) => match s {
    case digits: "number"         // "123" → "number"
    case alpha: "word"            // "hello" → "word"
    default: "other"              // "hello world" → "other"
}

// Mix literal and pattern arms
type num = \(d+)
fn tag(s) => match s {
    case "hello": "greeting"      // literal match checked first
    case num: "number"
    default: "unknown"
}
```

## Pattern-Aware String Functions

Named patterns can be passed to `find()`, `replace()`, and `split()` as the match argument. These functions use **partial/search** semantics (find matches *within* the string), unlike `is` and `match` which require full-string matches.

```lambda
type digits = \(d+)
type ws = \(s+)

// find(str, pattern) → [{value, index}, ...]
find("a1b22c333", digits)
// [{value: "1", index: 1}, {value: "22", index: 3}, {value: "333", index: 6}]

// replace(str, pattern, replacement) → str
replace("a1b2c3", digits, "N")        // "aNbNcN"
replace("hello   world", ws, " ")     // "hello world"

// split(str, pattern) → [str, ...]
split("a1b2c3", digits)               // ["a", "b", "c", ""]
split("a1b2c3", digits, true)         // ["a", "1", "b", "2", "c", "3", ""]  — keep delimiters
```

All three functions also accept plain strings as the match argument (see [Lambda_Sys_Func.md](Lambda_Sys_Func.md) § String Functions). `find` and `replace` see the same matches, found as ECMAScript `replaceAll` finds them, so a pattern that can match the empty string yields empty matches too, and `replace` inserts its replacement as literal text (S17.6.1).
