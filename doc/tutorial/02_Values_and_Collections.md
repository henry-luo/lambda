# 2. Values and Collections

Everything a Lambda program handles is a **value**: a number, a string, a list of books, a whole HTML document. This chapter tours the kinds of values, how to write them, and how to take them apart. Follow along in the REPL (`lambda`) or in script files.

## Numbers

```text repl
λ> 7 / 2
3.5
λ> 7 div 2
3
λ> 7 % 2
1
λ> 2 ** 10
1024
λ> type(42)
int
λ> type(3.5)
float
```

`/` always divides exactly; `div` is integer division. Integers (`int`) are exact across the range of ±(2⁵³−1), and floats (`float`) are 64-bit IEEE numbers — with the usual binary rounding:

```text repl
λ> 0.1 + 0.2
0.30000000000000004
λ> 0.1m + 0.2m
0.3
λ> 19.99m * 3
59.97
λ> type(19.99m)
decimal
```

The `m` suffix makes a **decimal**, an exact base-10 number for money and measurements. Lambda also has arbitrary-precision integers (`42n`) and sized numbers such as `255u8` and `3.14f32`; [Lambda_Data.md](../Lambda_Data.md#numeric-literals) lists them all.

## Strings and Symbols

Strings are UTF-8 text in double quotes. `++` joins them, `len` counts characters (not bytes), and a range subscript slices them:

```text repl
λ> "Hello" ++ ", " ++ "world"
"Hello, world"
λ> len("café")
4
λ> "lambda"[0 to 2]
"lam"
λ> upper("lambda")
"LAMBDA"
```

Single quotes make a **symbol** — an interned name such as a format name, a tag or an enumeration value. A symbol is never equal to a string with the same text; convert explicitly when you need to:

```text repl
λ> 'json' == "json"
false
λ> string('json')
"json"
λ> type('json')
symbol
```

## Truth and Absence

`true` and `false` are the booleans, and `null` means "nothing here". In a condition, only `false`, `null`, the empty string `""` and error values count as false — `0` and empty collections are true:

```text repl
λ> not ""
true
λ> if (0) "yes" else "no"
"yes"
λ> null == false
false
```

## Dates and Times

`t'…'` writes a date, a time or both, and the parts are members. The notation follows a subset of ISO 8601 (extended format, e.g. `2025-04-26T10:30:00Z`), so `2025`, `2025-06`, `10:30` and `+08` offsets are all accepted:

```text repl
λ> t'2025-04-26'.weekday
6
λ> t'2025-04-26T10:30:00'.format("MMM DD, YYYY")
"Apr 26, 2025"
```

## Arrays

An array is an ordered collection in square brackets. Indexes start at 0, `last` is the final index, and reading past the end gives `null` rather than an error:

```text repl
λ> let nums = [10, 20, 30]
λ> nums[0]
10
λ> nums[last]
30
λ> nums[5]
null
λ> nums[1 to 2]
[20, 30]
λ> len(nums)
3
λ> nums ++ [40]
[10, 20, 30, 40]
λ> nums * 2
[20, 40, 60]
```

`++` concatenates; arithmetic applies to every item (`nums * 2`), which [Chapter 3](03_Transforming_Data.md) puts to work.

## Maps

A map holds named fields in braces. `.` reads a field, and a missing field reads as `null`:

```text repl
λ> let book = {title: "Clean Code", year: 2008}
λ> book.title
"Clean Code"
λ> book.price
null
λ> {*:book, year: 2009}
{
  title: "Clean Code",
  year: 2009
}
```

Values never change in place, so "updating" a map means building a new one: `{*:book, year: 2009}` spreads `book`'s fields and overrides `year`.

## Elements

An **element** is Lambda's markup value: a tag, named attributes and ordered children, in one literal. Attributes come first, then a comma, then the children:

```lambda
// card.ls
let card = <div class: "card",
    <h2 "Clean Code">
    <p "Robert Martin, " <em "2008">>
>
card.name;
card.class;
card[0];
len(card);
card?<em>
```

```bash
lambda card.ls
```

```text
'div'
"card"
<h2
  "Clean Code">
3
<em
  "2008">
```

- `card.name` is the tag, as a symbol; `card.class` reads an attribute.
- `card[0]` is the first child. `len(card)` counts attributes and children together — one attribute and two children here.
- `card?<em>` searches the whole tree for `<em>` elements. Queries like this are how you take documents apart, starting in [Chapter 4](04_Documents_as_Data.md).

Elements are how Lambda represents HTML, XML, Markdown and every other document format, so the same operations work on all of them.

## Ranges

`a to b` is the inclusive run of integers — or single characters — from `a` to `b`:

```text repl
λ> 1 to 5
[1, 2, 3, 4, 5]
λ> "a" to "e"
["a", "b", "c", "d", "e"]
λ> len(1 to 100)
100
```

## Names with `let`

`let` binds a name to a value, and the binding is final: it cannot be reassigned. Top-level code and functions have no assignment at all; changing a variable is something only a procedure does, with `var` (see [Chapter 7](07_Procedures_IO_and_Tasks.md)).

```lambda error=E224
// immutable.ls
let year = 2008
year = 2009
```

```bash
lambda immutable.ls
```

```text partial
immutable.ls:3:1: error[E224]: assignment is only allowed inside a procedure (pn)
```

A binding may carry a type, which Lambda checks: `let year: int = 2008`. [Chapter 5](05_Types_and_Schemas.md) covers types.

## One Statement per Line

A line break ends a statement, with two exceptions that make long expressions easy to split:

- **An incomplete line continues.** A line ending in an operator, or with a bracket still open, carries on to the next line.
- **A line that can only continue does.** A line starting with `|>`, `and`, `or`, `++`, `==`, `else` or another token that cannot begin a statement joins the line before it.

Some tokens could either start a new statement or continue the previous one: `[`, `(`, `-`, `+`, `*`, `/`, `^`, `<` and `.` before a digit. Lambda refuses to guess. Save this as `scores.ls`:

```lambda error=E100
// scores.ls
let scores = [70, 85, 92]
let best = max(scores)
(best - 70) / 10
```

```bash
lambda scores.ls
```

```text partial
scores.ls:4:1: error[E100]: this token cannot continue the previous line; write ';' to start a new statement, or move it to the end of that line
```

Read literally, lines 3 and 4 could be one expression — `max(scores)(best - 70)`, a call of the result. The fix is the one the message suggests: end the previous line with `;`.

```lambda
// scores.ls
let scores = [70, 85, 92]
let best = max(scores);
(best - 70) / 10
```

```bash
lambda scores.ls
```

```text
2.2
```

This is the most common surprise for newcomers, and the rule is deliberately strict: a line break never silently splits or merges two statements (S16.2). When a line starts with `[`, `(` or `-`, check the line above it.

## Comments

`//` comments run to the end of the line, and `/* … */` comments can span lines.

## What You Learned

- Numbers come in exact (`int`, `decimal`) and binary (`float`) forms; `/` divides exactly and `div` truncates.
- Strings use double quotes, symbols single quotes, and the two never compare equal.
- Arrays, maps and elements are the three containers; missing indexes and fields read as `null`.
- `let` bindings are final; values are rebuilt rather than modified.
- End a line with `;` when the next one starts with `[`, `(`, `-` and similar tokens.

The reference for everything in this chapter is [Lambda_Data.md](../Lambda_Data.md). Next, [Chapter 3](03_Transforming_Data.md) turns collections into results.
