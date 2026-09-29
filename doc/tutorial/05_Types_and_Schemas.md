# 5. Types and Schemas

Every Lambda value has a type, and types are values too: you can write them after a name, combine them, name them, test values against them, and hand a file of them to the validator as a schema. This chapter covers annotations, the type forms you will use most, `is` and `match`, constrained types and string patterns, object types, and validating `books.json` from the command line.

## Annotations

A colon after a name gives its type. Annotations are optional — without one, Lambda infers the type from the value — but they document your intent, and Lambda checks them before the script runs. Save this as `typed.ls`:

```lambda
// typed.ls
let title: string = "Clean Code"
let published: int = 2008
fn age(year: int, now: int) int => now - year
age(published, 2026)
```

```bash
lambda typed.ls
```

```text
18
```

A parameter takes an annotation the same way, and the type after the parameter list is the function's return type. Now give a binding a value that does not fit; a map annotation is checked field by field. Save this as `typed_bad.ls`:

```lambda error=E201
// typed_bad.ls
let book: {title: string, year: int} = {title: "Clean Code", year: "2008"}
book.year + 1
```

```bash
lambda typed_bad.ls
```

```text partial
typed_bad.ls:2:1: error[E201]: field 'year' of 'book' expects int, but got string
```

Calls are checked too: `age("2008", 2026)` stops with error E207, *argument 1 expected int, got string*. Lambda never converts a value behind your back — `int("2008")` converts when you ask. Only numbers widen, and only exactly: an `int` is accepted where a `float` or a `decimal` is expected.

## Testing Values with `is`

`value is Type` asks whether a value belongs to a type. Try it in the REPL:

```text repl
λ> 2008 is int
true
λ> "2008" is int
false
λ> 2008 is number
true
λ> 2008 is float
true
λ> type(37.9)
float
λ> type(37.9) == float
true
```

`number` covers every numeric type. `type(v)` returns a value's type, and since a type is a value, you can compare it, store it in a `let`, or pass it to a function.

## Composing Types

Types combine much as expressions do. These are the forms you will use most:

| Type | Admits |
|---|---|
| `int \| string` | an int or a string |
| `int?` | an int or `null` — short for `int \| null` |
| `string[]` | an array whose items are all strings |
| `{title: string, year: int}` | a map with at least these two fields |
| `<a href: string, string>` | an `<a>` element with a string `href` and one string child |
| `"GET" \| "POST"` | exactly one of these two strings |

```text repl
λ> "n/a" is int | string
true
λ> null is int?
true
λ> ["craft", 42] is string[]
false
λ> {title: "SICP", year: 1985, pages: 657} is {title: string, year: int}
true
λ> {title: "SICP"} is {title: string, year: int}
false
λ> <a href: "/home", "Home"> is <a href: string, string>
true
λ> "PUT" is "GET" | "POST"
false
```

Map types are **open**: a map may carry fields the type does not mention, like `pages` above, but it must have every field the type names, with the right type (S11.4.6). In an element type, a `;` separates the attributes from the content. A literal such as `"GET"` is a type that admits exactly that value, so a union of literals is an enumeration.

## Naming Types

`type Name = …` names a type so you can reuse it. Save this as `book_types.ls`:

```lambda
// book_types.ls
type Tag = string
type Book = {title: string, author: string, year: int, price: float, tags: Tag[]}

let books = input("books.json")^;
[books is Book[], books[0] is Book, {title: "Draft"} is Book]
```

```bash
lambda book_types.ls
```

```text
[true, true, false]
```

`input(...)` reads and parses the file, as in [Chapter 4](04_Documents_as_Data.md); the `^` after it passes a read failure on, which [Chapter 6](06_Functions_and_Errors.md) explains. `Book[]` is an array of books, so one `is` checks the whole catalog.

## Matching on Types

`match` tries its `case` arms in order and takes the first whose type admits the value; `default` catches everything else. Any type form works as a case. Save this as `cells.ls`, which formats a field of a book for a report:

```lambda
// cells.ls
fn cell(v) => match v {
    case null: "-"
    case int | float: string(v)
    case string: v
    case string[]: join(v, ", ")
    default: "?"
}

let books = input("books.json")^
let book = books[0];
[cell(book.title), cell(book.year), cell(book.tags), cell(book.isbn)]
```

```bash
lambda cells.ls
```

```text
["The Pragmatic Programmer", "1999", "craft, career", "-"]
```

The book has no `isbn` field, so `book.isbn` is `null` and takes the first arm. Arm order matters: put narrow types before the broad ones that would also admit them.

## Constrained Types

`that` narrows a type with a condition, in which `~` stands for the value being checked. Name the result like any other type. Save this as `prices.ls`:

```lambda
// prices.ls
type Price = float that (~ > 0)
type Year = int that (1450 <= ~ and ~ <= 2100)

fn band(p) => match p {
    case float that (~ >= 100): "premium"
    case Price: "regular"
    default: "invalid"
}

[37.9 is Price, -5.0 is Price, 2008 is Year, 1200 is Year];
[band(190.0), band(37.9), band(-5.0)]
```

```bash
lambda prices.ls
```

```text
[true, false, true, false]
["premium", "regular", "invalid"]
```

Today the condition runs in `is` and `match`. An annotation, a map field and the validator check only the base type (S11.4.6): `let p: Price = -5.0` is accepted, because `-5.0` is a `float`. Test with `is` wherever the condition matters.

## String Patterns

A **string pattern** is a type that describes text, written `\( … )`. Inside it, `d` is a digit, `w` a word character and `a` a letter; quoted text is literal; and `+`, `*`, `?` and `{2,6}` count repetitions, as in a regular expression. Save this as `patterns.ls`:

```lambda
// patterns.ls
type Digits = \(d+)
type Email = \(w+ "@" w+ "." a{2,6});
["2008" is Digits, "20o8" is Digits, "ada@lambda.dev" is Email, "ada@lambda" is Email];
replace("ISBN 978-0132350884", Digits, "#")
```

```bash
lambda patterns.ls
```

```text
[true, false, true, false]
"ISBN #-#"
```

`is` matches the whole string, while `replace`, `find` and `split` look for the pattern inside it. Because a pattern is a type, it also works in annotations and `match` arms. [Lambda_String_Pattern.md](../Lambda_String_Pattern.md) describes the full pattern language.

## Object Types

`type Book = {…}` names a *shape*: any map with those fields is a `Book`. Leave out the `=` and you declare an **object type** instead — a new, *nominal* type whose values are built with its name and which can carry methods. Save this as `book_object.ls`:

```lambda
// book_object.ls
type Book {
    title: string,
    year: int,
    fn age(now) => now - year
}

let b = <Book title: "Clean Code", year: 2008>;
b;
b.age(2026);
[b is Book, b is map, {title: "Clean Code", year: 2008} is Book]
```

```bash
lambda book_object.ls
```

```text
<Book title: "Clean Code", year: 2008>
18
[true, true, false]
```

- `<Book …>` builds an object, and its fields are checked as it is built: leaving out `year` is error E205, a string `year` error E201.
- A method is a function declared inside the type. It reads the object's fields by their bare names (`year`), and you call it with a dot: `b.age(2026)`.
- An object is still a map, but a plain map with the same fields is not a `Book`: object types match by name, not by shape.
- `<Book *:b, year: 2009>` copies an object with changes, as `{*:m, …}` does for a map.

Object bodies also parse `that` conditions on fields, but they are not enforced yet; put conditions on a named type and test them with `is`.

## Validating Files

A schema is an ordinary file of `type` declarations. Save this as `book_schema.ls`:

```lambda
// book_schema.ls
type Book = {
    title: string,
    author: string,
    year: int,
    price: float,
    tags: string[]
}
type Catalog = Book[]
```

`lambda validate` parses a data file and checks it against the type named `Document` if the schema has one, and otherwise against the **last** type in the file — here `Catalog`:

```bash
lambda validate books.json -s book_schema.ls
```

```text
Starting validation with arguments
Starting validation of 'books.json' using schema 'book_schema.ls'...
Lambda AST Validator v2.0
Validating 'books.json' using schema-based validation
Format: json, Schema: book_schema.ls
Validation options:
  - Strict mode: disabled
  - Max errors: 100
  - Max depth: 100
  - Allow unknown fields: no
Loading schema and parsing data file...
Successfully parsed input file
Validating data against schema...

=== Validation Results ===
✓ Validation successful
Errors: 0, Warnings: 0
```

Now break the data. Save this as `bad_books.json`, with a year in quotes, a missing author and a number among the tags:

```json file=bad_books.json
[
  {"title": "Clean Code", "author": "Robert Martin", "year": "2008", "price": 37.9, "tags": ["craft"]},
  {"title": "SICP", "year": 1985, "price": 55.0, "tags": ["lisp", 1985]}
]
```

```bash
lambda validate bad_books.json -s book_schema.ls
```

```text partial
✗ Validation failed
Errors: 3, Warnings: 0
  1. [TYPE_MISMATCH] Expected type 'int', but got 'string' at [0].year
  2. [MISSING_FIELD] Required field 'author' is missing from object at [1].author
  3. [TYPE_MISMATCH] Expected type 'string', but got 'int' at [1].tags[1]
```

Each error carries a path into the document — `[1].tags[1]` is the second tag of the second book — and some add a suggestion, such as removing the quotes around `"2008"`. The command exits with a non-zero status when validation fails, so it can guard each end of a pipeline: validate the input, transform it, validate the output. [Lambda_Validator.md](../Lambda_Validator.md) covers the options, such as `--max-errors`, and the built-in schemas for HTML, Markdown and other formats.

## What You Learned

- `name: Type` annotates a `let` or a parameter, and a type after the parameter list annotates the result; a mismatch Lambda can see in the source is a compile error.
- `is` tests a value against a type; `|`, `?`, `[]`, `{…}`, `<…>` and literals compose types, and map types are open.
- `type Name = …` names a type, and `match` takes the first arm whose type admits the value.
- `that` adds a condition, checked by `is` and `match`; `\(…)` patterns describe text.
- `type Name { … }` declares a nominal object type with fields and methods.
- A schema is a file of types, and `lambda validate data -s schema.ls` checks a file against its `Document` or last type.

The reference for types is [Lambda_Type.md](../Lambda_Type.md). Next, [Chapter 6](06_Functions_and_Errors.md) writes functions and handles errors.
