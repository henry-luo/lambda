# Lambda Validator Guide

## Overview

Lambda has no separate schema language: a **schema is a set of `type` declarations** in an ordinary `.ls` file, and the validator checks a parsed document against one of them. The same type syntax that annotates program values (see [Lambda_Type.md](Lambda_Type.md)) describes JSON objects, YAML and TOML configuration, CSV rows, XML and HTML element trees, and the prose documents that Markdown, RST, AsciiDoc and wiki markup parse into (S11.4).

The validator runs from the command line:

```bash
lambda validate data.json -s schema.ls          # your own schema
lambda validate page.html                       # a built-in schema chosen by format
```

> **Not yet available:** calling the validator from inside a Lambda script. The `validate(...)`, `load_schema(...)` and `validate_with_*` functions described in earlier versions of this guide do not exist; the C++ `SchemaValidator` API behind the command is not exposed to scripts yet. Validate at the command line around the script that transforms the data.

## Writing a Schema

### Basic Types

```lambda
// primitive types
type Name = string
type Age = int
type Score = float
type Active = bool

// optional types (can be null)
type OptionalEmail = string?
type OptionalAge = int?

// arrays
type Names = string[]
type Numbers = int[]
type OptionalList = string[]?  // the array itself is optional

// maps
type Person = {
    name: string,
    age: int,
    email: string?
}
```

### Homogeneous Arrays and Occurrence Patterns

`T[]` is the homogeneous array contract: every element must satisfy `T`, including values admitted from dynamic input. Bracket occurrence forms are structural patterns over a run of items; they are not alternate spellings of `T[]` (**S11.1.1v3**, **S11.4.1v3**).

```lambda
type Document = {
    // homogeneous strings, including the empty array
    tags: string[],

    // zero or more elements (can be empty) — bracket form
    comments: [string*],

    // one or more elements (must have at least one)
    authors: [string+],

    // optional array (can be missing entirely)
    attachments: string[]?
}
```

### Nested Structures

```lambda
type Address = {
    street: string,
    city: string,
    zipcode: string
}

type Company = {
    name: string,
    address: Address
}

type Person = {
    name: string,
    age: int,
    company: Company?  // optional nested structure
}

// recursive types for tree structures
type Node = {
    value: int,
    children: [Node*]  // zero or more child nodes
}
```

### Union and Literal Types

```lambda
type Id = int | string
type Method = "GET" | "POST" | "PUT" | "DELETE"
type Result = {success: true, data: any} | {success: false, error: string}
```

### Constrained Types and String Patterns

A `that` clause attaches a runtime predicate, with `~` the value being checked; a string pattern constrains text (see [Lambda_Type.md](Lambda_Type.md#constrained-types-that) and [Lambda_String_Pattern.md](Lambda_String_Pattern.md)):

```lambda
type Age = int that (~ >= 0 and ~ <= 150)
type Email = \("\w"+ "@" "\w"+ "." "\a"{2,6})
type User = {name: string, age: Age, email: Email}

30 is Age            // true
200 is Age           // false
"a@b.com" is Email   // true
```

> **Not yet implemented.** The `is` checks above work, but `lambda validate` does not yet apply the same rules to a schema: a constrained type such as `Age` checks only its base type (S11.4.6), and a string pattern used through a **name** (`email: Email`) rejects every string. Until this is fixed, write a pattern inline in the field — `email: \("\w"+ "@" "\w"+ "." "\a"{2,6})` — which the validator does check.

### Element Schemas

XML, HTML and the prose formats parse into element trees, which element types describe. Attributes come before the `;`, content after it, with the occurrence markers `?`, `*` and `+`:

```lambda
// XML article schema
type Article = <article
    title: string,
    author: string,
    date: string?,
    tags: [string*],
    <content>,
    <metadata id: string, status: string>?
>

// HTML page schema
type Page = <html
    <head
        <title>,
        <meta name: string, content: string>*
    >,
    <body
        <h1>+,
        <p>*,
        <div class: string?>*
    >
>
```

### Which Type Is the Root

A schema file may define many types. The validator checks the document against the type named **`Document`** if the file defines one, and otherwise against the **last type defined** in the file. Put the root type last, or call it `Document`:

```lambda
type Address = {street: string, city: string}
type Person = {name: string, age: int, address: Address?}
type Document = {members: [Person+], tags: string[]}   // the root
```

## Running the Validator

```
lambda validate [-s <schema>] [-f <format>] [options] <file>
```

| Option | Meaning | Default |
|--------|---------|---------|
| `-s <schema.ls>` | Schema file. Required for data formats; formats with a built-in schema may omit it | built-in by format |
| `-f <format>` | Input format when the extension does not identify it | auto-detect |
| `--strict` | Accepted, but has no effect yet | off |
| `--max-errors N` | Stop after N errors | 100 |
| `--max-depth N` | Maximum nesting depth to validate | 100 |
| `--allow-unknown` | Accepted, but has no effect: map types are open, so undeclared fields always pass (S11.4.6) | off |

One file is validated per invocation. The exit status is 0 when the document is valid and non-zero otherwise, and the report names each problem with a path into the document:

```text
=== Validation Results ===
✗ Validation failed
Errors: 1, Warnings: 0

Errors:
  1. [TYPE_MISMATCH] Expected type 'int', but got 'string' at .age
```

For near-miss field names the report suggests the closest declared field.

### Input Formats

Auto-detected from the extension: `.json`, `.csv`, `.ini`, `.toml`, `.yaml`/`.yml`, `.xml`, `.md`/`.markdown`, `.rst`, `.html`/`.htm`, `.wiki`, `.adoc`/`.asciidoc`, `.1`–`.9` (man pages), `.eml`, `.ics`, `.vcf`, `.textile`/`.txtl`, `.mark`. Others take `-f`: `latex`, `rtf`, `pdf`, `text`.

Mark auto-detection recognizes only `.mark` (D2.9.1); explicit `-f mark`
accepts any filename (D2.9.2).

### Built-in Schemas

Some formats have a schema shipped with the runtime (under `LAMBDA_HOME`, at `package/doc/*_schema.ls`), so `-s` may be omitted:

| Format | Schema | Root type |
|--------|--------|-----------|
| `html` | `html5_schema.ls` | `HTMLDocument` |
| `eml` | `eml_schema.ls` | `EMLDocument` |
| `ics` | `ics_schema.ls` | `ICSDocument` |
| `vcf` | `vcf_schema.ls` | `VCFDocument` |
| `markdown`, `rst`, `asciidoc`, `wiki`, `textile`, `man` | `doc_schema.ls` — the [Mark Doc schema](Doc_Schema.md) | `Document` |
| `.ls` | the Lambda AST validator (no schema file) | |

`json`, `xml`, `yaml`, `csv`, `ini`, `toml`, `latex`, `rtf`, `pdf` and `text` need `-s`.

### Format Handling

- **XML** — a document wrapper (`<document …>`) produced by the parser is unwrapped and the root element is validated.
- **HTML** — the `<body>` element and its contents are validated against the HTML5 schema.
- **JSON, YAML, TOML, INI, CSV** — the parsed maps and arrays are validated directly; CSV is an array of row maps.

### Examples

```bash
lambda validate data.json -s schema.ls
lambda validate page.html
lambda validate README.md
lambda validate --strict config.yaml -s config_schema.ls --max-errors 50
lambda validate export.txt -f csv -s rows_schema.ls --allow-unknown
```

## Validation Semantics

- **Type mismatch**: a value that does not satisfy its declared type is reported with the expected and actual types and the path.
- **Missing fields**: a required field (no `?`) must be present. An optional field may be absent or `null`.
- **Unknown fields**: fields the map type does not declare pass, as they do for `is`: map types are open (S11.4.6). A closed map type is an open design question (SO9).
- **Occurrence**: `[T+]` requires at least one item, `[T*]` accepts none, `T[]` requires every item to satisfy `T`.
- **Depth and error limits**: validation stops at `--max-depth` levels and after `--max-errors` reports.

## Best Practices

### Organize Schemas into Reusable Types

```lambda
// common types
type EmailAddress = string     // a named pattern here is rejected by the validator today (see above)
type Timestamp = string

// domain types
type User = {
    id: int,
    email: EmailAddress,
    phone: string?,
    created: Timestamp
}

type Document = {
    id: int,
    author: User,
    title: string,
    content: string,
    published: Timestamp?
}
```

### Optional vs Null

```lambda
type Person = {
    name: string,
    age: int,
    email: string?  // may be missing or null
}
```

`{name: "Alice", age: 30, email: null}` and `{name: "Bob", age: 25}` are both valid.

### Choose the Right Array Form

```lambda
type Article = {tags: [string*]}       // zero or more tags
type Team = {members: [string+]}       // at least one member
type Report = {attachments: string[]?} // the whole array may be missing
```

### Prefer Small Schemas Per Document Kind

Keep one schema file per document kind with its root type last (or named `Document`), and validate at the boundaries of a pipeline: the input before a transformation and the output before it is written.

## Troubleshooting

| Issue | Cause and fix |
|-------|---------------|
| "Unknown type" | A type is used before it is defined, or is misspelled. Define it in the same schema file |
| Everything validates | The root type may not be the one you expect: name it `Document` or move it last |
| Extra fields not reported | Map types are open, so undeclared fields always pass; there is no closed-map option yet |
| A valid string fails a pattern | A pattern used through a name fails in the validator; write it inline in the field |
| "Maximum depth exceeded" | Raise `--max-depth`, or check for a recursive type that admits cyclic data |
| Too many errors | `--max-errors 10` stops early so the first problems are readable |

## Further Reading

- [Lambda_Type.md](Lambda_Type.md) — the type language: maps, arrays, unions, occurrences, element types, constrained types, string patterns
- [Doc_Schema.md](Doc_Schema.md) — the Mark Doc schema every prose format validates against
- [Lambda_CLI.md](Lambda_CLI.md) — the `validate` command reference
- [Lambda_Reference.md](Lambda_Reference.md) — language overview
