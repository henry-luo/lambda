# Markup & Data Format Support

Lambda parses a wide variety of text and binary formats into a single, uniform in-memory representation — the **Lambda/Mark node tree** — so that all downstream transformation, validation, layout and rendering code works on the same data model regardless of the original source format.

```
[Source file]  →  [parser]  →  [Lambda/Mark tree]  →  [transform / validate / format / render]
```

Every tree shown below is the real output of `lambda convert <file> -t mark` on the current build; where a parser's output differs from the shape it is heading for, the difference is stated. This document covers:

1. [Lightweight markup languages](#1-lightweight-markup-languages) — human-authored prose that maps to the **Mark Doc** element schema
2. [Data-interchange formats](#2-data-interchange-formats) — structured data that maps to Lambda **maps, arrays and elements**
3. [Other formats](#3-other-formats) — LaTeX, PDF, email, contacts, calendars, diagrams, CSS, MDX and directories
4. [Reading and writing](#4-reading-and-writing) — `input()`, `parse()`, `format()` and `lambda convert`
5. [Output formats](#5-output-formats)

---

## 1. Lightweight Markup Languages

All lightweight markup flavors (Markdown, reStructuredText, AsciiDoc, …) parse into the same **Mark Doc schema** — an element tree rooted at `<doc>` (see [Doc_Schema.md](Doc_Schema.md)). Block structure (headings, paragraphs, lists, code blocks, tables) and inlines (emphasis, links, code spans) use HTML element names wherever HTML has one, plus a few custom elements for features HTML lacks (math, citations, footnotes).

### 1.1 Supported Flavors

| Format | Input type string | Notes |
|--------|:-----------------:|-------|
| CommonMark / GitHub Flavored Markdown | `markdown` (`md`) | passes the CommonMark test suite; `{type: 'markup', flavor: 'commonmark'}` selects strict CommonMark |
| reStructuredText | `rst` | |
| MediaWiki / DokuWiki markup | `wiki` | |
| AsciiDoc | `asciidoc` / `adoc` | |
| Emacs Org-mode | `org` | |
| Textile | `textile` | |
| troff/man pages | `man` | |
| MDX (Markdown + JSX) | `mdx` | see §3.9 |
| LaTeX | `latex` | see §3.1 — a `latex_document` tree, not Mark Doc |
| Typst | `typst` | |
| HTML5 | `html` / `html5` | passes the html5lib test suite; see §1.4 |
| Mark | `mark` | Lambda's own notation, read back losslessly |

In Lambda scripts all of these are loaded with `input()`:

```lambda
let md   = input("readme.md",    'markdown')^
let rst  = input("design.rst",   'rst')^
let wiki = input("page.wiki",    'wiki')^
let adoc = input("guide.adoc",   'asciidoc')^
let html = input("index.html",   'html')^
```

### 1.2 Unified Mark Doc Schema

Every prose parser produces the same element-tree shape. Element names are aligned with HTML so the tree passes straight to the Radiant layout engine.

| Category | Mark element(s) | Notes |
|----------|-----------------|-------|
| Document root | `<doc version: "1.0", <body …>>` | the `<meta>` element is part of the schema but not produced yet; Markdown front matter currently parses as a thematic break and a heading |
| Headings | `<h1 level: "1">` … `<h6 level: "6">` | `level` is emitted as a string |
| Paragraph | `<p <span …>>` | the inline run is wrapped in one `<span>` |
| Block quote | `<blockquote>` | |
| Code block | `<code type: "block", language: "python">` | |
| Horizontal rule | `<hr>` | |
| Unordered list / item | `<ul>` / `<li>` | |
| Ordered list / item | `<ol start: N>` / `<li>` | |
| Definition list | `<dl>` / `<dt>` / `<dd>` | |
| Table | `<table>` / `<thead>` / `<tbody>` / `<tr>` / `<th>` / `<td>` | |
| Figure / caption | `<figure>` / `<figcaption>` | |
| Bold / strong | `<strong>` | |
| Italic / emphasis | `<em>` | |
| Strikethrough | `<del>` | |
| Inline code | `<code type: "inline">` | |
| Hyperlink | `<a href: …>` | |
| Image | `<img src: …, alt: …>` | |
| Line break | `<br>` | |
| Math | `<math type: "inline">`, `<math type: "block">` | the content is the TeX source without delimiters |
| Footnote | `<footnote-ref ref: "1">` in the text; `<footnote>` holds the text | custom elements |
| Citation | `<citation key: "smith2020">` | custom element |
| Emoji shortcode | `'smile'` | currently a bare symbol, not converted to the character |

### 1.3 Side-by-Side: Markdown → Mark Doc

<table>
<thead>
<tr><th>Markdown source</th><th>Mark Doc (Lambda element tree)</th></tr>
</thead>
<tbody>
<tr>
<td>

```markdown
# Hello World

Some **bold** and *italic* text
with a [link](https://example.com).

- item one
- item two

| Name  | Age |
|-------|-----|
| Alice | 30  |
```

</td>
<td>

```mark
<doc version: "1.0",
  <body
    <h1 level: "1", "Hello World">
    <p
      <span
        "Some " <strong "bold"> " and " <em "italic">
        " text with a "
        <a href: "https://example.com", "link">
        ".">>
    <ul
      <li "item one">
      <li "item two">>
    <table
      <thead <tr <th "Name"> <th "Age">>>
      <tbody <tr <td "Alice"> <td "30">>>>>>
```

</td>
</tr>
</tbody>
</table>

### 1.4 HTML → Mark

HTML5 is a first-class input format and is preserved verbatim, whitespace text nodes included: the root is the `<#document>` node with its doctype, and the `<html>`, `<head>` and `<body>` elements come through as parsed. There is no `<doc>` wrapper.

<table>
<thead>
<tr><th>HTML5 source</th><th>Mark (Lambda element tree)</th></tr>
</thead>
<tbody>
<tr>
<td>

```html
<!DOCTYPE html>
<html lang="en">
<head><title>Page</title></head>
<body>
  <h1 class="title">Hello</h1>
  <p>A <em>simple</em> page.</p>
</body>
</html>
```

</td>
<td>

```mark
<#document
  <#doctype name: "html">
  <html lang: "en",
    <head <title "Page">>
    "\n"
    <body
      "\n  "
      <h1 class: "title", "Hello">
      "\n  "
      <p "A " <em "simple"> " page.">
      "\n\n\n">>>
```

</td>
</tr>
</tbody>
</table>

---

## 2. Data-Interchange Formats

Structured data formats map to Lambda's **map** (`{…}`), **array** (`[…]`) and **element** (`<tag …>`) values. Once parsed you have a live Lambda value that you can query, transform and export to any other format.

### 2.1 Supported Formats

| Format | Input type string | Lambda data model |
|--------|:-----------------:|:-----------------:|
| JSON | `json` | maps + arrays |
| XML | `xml` | element tree |
| YAML 1.2 | `yaml` | maps + arrays; passes the YAML 1.2 test suite |
| TOML | `toml` | maps + arrays |
| CSV | `csv` | array of maps |
| INI | `ini` | nested maps, typed values |
| Java `.properties` | `properties` | flat map, typed values |
| Mark | `mark` | any Lambda value |
| SQLite database | (auto-detected) | relational tables read as arrays of row maps |

### 2.2 JSON → Lambda Map / Array

JSON is the closest format to Lambda's native literal syntax. Object keys become map keys and value types are preserved.

<table>
<thead>
<tr><th>JSON</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```json
{
  "name": "Alice",
  "age": 30,
  "active": true,
  "score": 9.5,
  "tags": ["dev", "python"],
  "address": {
    "city": "New York",
    "zip": "10001"
  },
  "nickname": null
}
```

</td>
<td>

```lambda
{
  name:    "Alice",
  age:     30,
  active:  true,
  score:   9.5,
  tags:    ["dev", "python"],
  address: {
    city: "New York",
    zip:  "10001"
  },
  nickname: null
}
```

</td>
</tr>
</tbody>
</table>

**Loading and querying in Lambda:**

```lambda
let data = input("users.json", 'json')^
data.name               // "Alice"
data.tags[0]            // "dev"
data.address.city       // "New York"
```

### 2.3 XML → Lambda Element Tree

XML maps one-to-one onto Lambda **elements**: a tag, attributes, and ordered children. The parser wraps the result in a `<document>` element that also carries the XML declaration, and attribute values stay **strings** (`id: "1"`, never `id: 1`).

<table>
<thead>
<tr><th>XML</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```xml
<?xml version="1.0"?>
<library>
  <book id="1" lang="en">
    <title>Clean Code</title>
    <author>Robert Martin</author>
    <year>2008</year>
  </book>
</library>
```

</td>
<td>

```mark
<document
  <?xml "version=\"1.0\"">
  <library
    <book id: "1", lang: "en",
      <title "Clean Code">
      <author "Robert Martin">
      <year "2008">>>>
```

</td>
</tr>
</tbody>
</table>

**Querying with the `?` operator** — a child element's text is its first child, so project with `[0]`:

```lambda
let lib = input("library.xml", 'xml')^
lib?<book>                    // all book elements
lib?<book lang: "en">         // books whose lang attribute is "en"
lib?<title> |> ~[0]           // ["Clean Code", …] — every title's text
lib?<book>?<year> |> int(~[0])   // [2008, …]
```

### 2.4 YAML → Lambda Map / Array

YAML documents (including multi-document streams) map to the same map/array model as JSON. All YAML scalar types — strings, ints, floats, booleans, nulls and timestamps — become their Lambda equivalents.

<table>
<thead>
<tr><th>YAML</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```yaml
server:
  host: localhost
  port: 8080
  tls: true

database:
  engine: postgres
  name: mydb
  pool: 5

features:
  - auth
  - logging
  - metrics
```

</td>
<td>

```lambda
{
  server: {
    host: "localhost",
    port: 8080,
    tls:  true
  },
  database: {
    engine: "postgres",
    name:   "mydb",
    pool:   5
  },
  features: ["auth", "logging", "metrics"]
}
```

</td>
</tr>
</tbody>
</table>

### 2.5 TOML → Lambda Map

TOML section headers become nested map keys; dotted keys are expanded into nested maps and arrays of tables into arrays of maps.

<table>
<thead>
<tr><th>TOML</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```toml
title = "My App"
version = "1.0.0"

[server]
host = "0.0.0.0"
port = 3000

[server.tls]
enabled = true
cert = "/etc/ssl/cert.pem"

[[plugins]]
name = "auth"
enabled = true

[[plugins]]
name = "cache"
enabled = false
```

</td>
<td>

```lambda
{
  title:   "My App",
  version: "1.0.0",
  server: {
    host: "0.0.0.0",
    port: 3000,
    tls: {
      enabled: true,
      cert: "/etc/ssl/cert.pem"
    }
  },
  plugins: [
    {name: "auth",  enabled: true},
    {name: "cache", enabled: false}
  ]
}
```

</td>
</tr>
</tbody>
</table>

### 2.6 CSV → Lambda Array of Maps

The first row is the header and supplies the map keys for every following row. Values are **strings**; a missing trailing field is `null`. Convert numbers where you use them.

<table>
<thead>
<tr><th>CSV</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```csv
name,age,city,score
Alice,30,New York,9.5
Bob,25,Los Angeles,8.2
Carol,35,Chicago,9.8
```

</td>
<td>

```lambda
[
  {name: "Alice", age: "30", city: "New York",    score: "9.5"},
  {name: "Bob",   age: "25", city: "Los Angeles", score: "8.2"},
  {name: "Carol", age: "35", city: "Chicago",     score: "9.8"}
]
```

</td>
</tr>
</tbody>
</table>

**Typical processing pipeline:**

```lambda
let rows = input("data.csv", 'csv')^

// filter and project
for (r in rows where int(r.age) >= 30)
  {name: r.name, city: r.city}
```

### 2.7 INI / .properties

Flat or lightly nested configuration formats map to one or two levels of maps, and their values are **typed**: `5432` is an int, `true`/`yes`/`on` a bool, `null`/`empty` a `null`, `3.14` a float. Keys before the first `[section]` land in a `global` map.

<table>
<thead>
<tr><th>INI</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```ini
[database]
host = localhost
port = 5432
name = mydb
ssl = true

[cache]
host = localhost
port = 6379
ttl  = 300
```

</td>
<td>

```lambda
{
  database: {
    host: "localhost",
    port: 5432,
    name: "mydb",
    ssl: true
  },
  cache: {
    host: "localhost",
    port: 6379,
    ttl:  300
  }
}
```

</td>
</tr>
</tbody>
</table>

`.properties` files (no sections) become a flat map whose keys keep their dots:

```lambda
// app.properties:  app.name=MyApp\napp.version=2.0\nserver.port=8080
let cfg = input("app.properties", 'properties')^
cfg.'app.name'        // "MyApp"
cfg.'server.port'     // 8080
```

### 2.8 Format Conversion

A parsed value can be written back out in any format that has a formatter (§5), through the `format()` function or the `lambda convert` command:

```lambda
// In a Lambda script
let data = input("config.yaml", 'yaml')^
format(data, 'json')          // → JSON string
format(data, 'toml')          // → TOML string
```

```bash
# CLI
lambda convert config.yaml -t json -o config.json
lambda convert data.csv  -t yaml -o data.yaml
lambda convert page.md   -t html -o page.html
```

Not every direction exists: there is no CSV writer, so a CSV can be read and converted to any other format but nothing converts *to* CSV, and the prose formats AsciiDoc, man and Typst are read-only as well.

---

## 3. Other Formats

### 3.1 LaTeX

LaTeX source (`.tex`) is parsed by a direct (hand-written) parser into a `latex_document` tree whose elements are named after the commands and environments they came from — `<documentclass>`, `<section title: <curly_group …>>`, `<textbf>`, `<paragraph>` — rather than into the Mark Doc schema. The Lambda-side `latex` package turns that tree into HTML; `lambda view` and `lambda convert paper.tex -t html` use it.

```lambda
let doc = input("paper.tex", 'latex')^
doc?<section> |> ~.title[0]           // section titles
```

```mark
<latex_document
  <documentclass "article">
  <document
    <section title: <curly_group "Basic Test">>
    <paragraph "This is a basic LaTeX document.">
    <paragraph <textbf "Bold text"> " and " <textit "italic text"> ".">>>
```

`format(doc, 'html')` serializes this raw tree; for rendered HTML use `lambda convert paper.tex -t html -o paper.html --full-document`. Math inside the document is parsed by the math parser (see [Math_Support.md](Math_Support.md)).

### 3.2 PDF

PDF documents are parsed into an element tree with best-effort text-flow reconstruction; compressed streams are decoded first. A separate PDF package written in Lambda interprets page content streams for viewing and rendering.

```lambda
let report = input("annual_report.pdf", 'pdf')^
report?<p> |> ~[0]             // first text of each paragraph
```

### 3.3 RTF

The RTF parser is **experimental**: it currently yields the document's control groups as raw maps (`{content: [...], formatting: {pard: true, qc: true, fs: 28, …}}`) rather than a Mark Doc tree, so RTF is not yet usable for conversion.

### 3.4 Email (EML / RFC 822)

E-mail files parse into a map of the well-known headers plus a `headers` map with every header as written; `date` is kept as the header string, and `body` holds the text when the message has one. `.eml` is not auto-detected by `lambda convert`, so give the type explicitly.

<table>
<thead>
<tr><th>EML source (excerpt)</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```
From: sender@example.com
To: recipient@example.com
Subject: Simple Test Email
Date: Mon, 1 Jan 2024 12:00:00 +0000
Message-ID: <simple@example.com>

Hi Bob, just checking in.
```

</td>
<td>

```lambda
{
  from:       "sender@example.com",
  to:         "recipient@example.com",
  subject:    "Simple Test Email",
  date:       "Mon, 1 Jan 2024 12:00:00 +0000",
  message_id: "<simple@example.com>",
  headers: {
    from: "sender@example.com", to: "recipient@example.com",
    subject: "Simple Test Email",
    date: "Mon, 1 Jan 2024 12:00:00 +0000",
    'message-id': "<simple@example.com>"
  },
  body:       "Hi Bob, just checking in."
}
```

</td>
</tr>
</tbody>
</table>

```lambda
let mail = input("message.eml", 'eml')^
mail.subject
```

### 3.5 vCard (VCF)

One card parses into a map with descriptive keys: `version`, `full_name`, a structured `name` (`family`, `given`, `additional`, `prefix`, `suffix`), `email`, `phone`, `organization`, `title`, structured `address` maps, `url`, `birthday`, and `note`. Its `entries` array preserves every property in source order, including repeated fields, with `name`, `value`, and optional `parameters` fields. Use `entries` to inspect repeated values and parameter types; the `properties` map remains a convenience view. A file with several cards returns `{contacts: [...]}`, with one map per card. The ordered entry and contact arrays use the sequence append of **D2.6.5v3**.

```lambda
let contact = input("alice.vcf", 'vcf')^
contact.full_name      // "Alice Wonderland"
contact.email          // "alice@example.com"
contact.phone          // "+1-555-0100"
contact.name.given     // "Alice"

let cards = input("contacts.vcf", 'vcf')^
cards.contacts[0].full_name
[for (entry in cards.contacts[0].entries where entry.name == "email") entry.value]
```

### 3.6 iCalendar (ICS)

Calendar files parse into a flat map — `version`, `product_id`, `calendar_scale`, `method` — with a `components` array. Each component carries its `type` (`"VEVENT"`, `"VTODO"`, …), the common properties by name (`uid`, `summary`, `description`, `location`, `status`, `organizer`, `attendee`), `start_time`/`end_time` as maps of date parts, and a `properties` map with every property as written.

```lambda
let cal = input("events.ics", 'ics')^
cal.components |: ~.type == "VEVENT" |> ~.summary    // ["Team standup", "Sprint review", …]
```

### 3.7 Graph and Diagram Formats

Diagram description languages parse into a `<graph>` element. Four flavors are supported through the `graph` type; the flavor comes from the file extension, or explicitly as `{type: 'graph', flavor: 'mermaid'}` in a script and `-f graph:mermaid` on the command line.

| Flavor | Explicit form | File extension |
|--------|:-------------:|:--------------:|
| Graphviz DOT | `graph:dot` | `.dot`, `.gv` |
| D2 diagrams | `graph:d2` | `.d2` |
| Mermaid diagrams | `graph:mermaid` | `.mmd` |
| Structurizr / C4 DSL | `graph:structurizr` | `.dsl`, `.structurizr` |

Mermaid and D2 produce `<node id: …, label: …, shape: …>` and `<edge from: …, to: …>` children. DOT keeps a source-preserving statement structure (`<dot-attr-statement>`, `<dot-edge-statement>` with `<dot-endpoint>` children, `<subgraph>`), so it can be written back exactly; every element also carries `source-line`/`source-column` attributes. The `graph` package lays these trees out for `lambda render` and `lambda view`.

```lambda
let g = input("flow.mmd", {type: 'graph', flavor: 'mermaid'})^
g?<node> |> ~.id                   // ["A", "B", "C"]
g?<edge> |> [~.from, ~.to]         // [["A", "B"], ["B", "C"]]
```

```mark
<graph type: "directed", flavor: "mermaid", kind: "flowchart",
  <node id: "A", label: "Start", shape: "box">
  <node id: "B", label: "Check", shape: "diamond">
  <edge from: "A", to: "B", style: "solid", arrow-end: "true">
  <node id: "C", label: "Done", shape: "box">
  <edge from: "B", to: "C", label: "yes", style: "solid", arrow-end: "true">>
```

### 3.8 CSS

`input(path, 'css')` parses a stylesheet for the Radiant engine. The result is an opaque stylesheet object, not a Lambda data structure: converting it to `mark` or `json` does not produce a rule list today, so CSS is not yet queryable as data.

### 3.9 JSX / MDX

MDX files interleave Markdown with JSX. The parser splits them into an `<mdx_document>` whose `<body>` holds the Markdown segments as `<doc>` trees and each JSX block as a `<jsx_element>` carrying the raw source in its `content` attribute; the components are not parsed into elements, so `page?<Card>` finds nothing.

```lambda
let page = input("Page.mdx", 'mdx')^
page?<jsx_element> |> ~.content    // the raw JSX blocks
```

### 3.10 Math

Mathematical notation is parsed standalone from LaTeX math or ASCII math into a `<math>` element tree with `parse()` (strings) or `input()` (files):

```lambda
let tex_expr = parse("\\frac{a}{b}", 'math')^
let ascii_expr = parse("(a + b) / c", 'math-ascii')^
let from_file = input("formula.tex", 'math')^
```

See [Math_Support.md](Math_Support.md).

### 3.11 Directory Listing

A directory path is an input too: it yields an array of **path** values, one per entry. Each has a `name`, and `string(p)` gives its spelling.

```lambda
let files = input("./src")^
files |: ends_with(~.name, ".cpp") |> ~.name       // all .cpp file names
```

---

## 4. Reading and Writing

All formats are read through the same two functions:

```lambda
// Explicit type
let typed_data = input("file.ext", 'json')^

// Auto-detect from the extension and the leading bytes
let auto_data = input("data.json")^

// Type with options, for formats that have flavors
let diagram = input("arch.mmd", {type: 'graph', flavor: 'mermaid'})^

// From a URL (HTTP/HTTPS)
let url_data = input("https://api.example.com/data.json")^

// From a string in memory
let str_data = parse("a: 1\nb: [1, 2]", 'yaml')^
```

`input` and `parse` raise on failure, so propagate with `^` or handle with `^ { … }`. **Auto-detection** inspects the file's leading bytes and its extension; `.eml` and `.ini` are exceptions and need an explicit type.

**CLI equivalent — `lambda convert`:**

```bash
lambda convert input.yaml  -t json   -o output.json
lambda convert input.md    -t html   -o output.html
lambda convert mail.eml    -f eml    -t json -o mail.json
lambda convert arch.mmd    -f graph:mermaid -t mark -o arch.mark
```

---

## 5. Output Formats

`format(value, 'type')` and `lambda convert -t <type>` write a Lambda value out:

| Category | Output formats |
|----------|----------------|
| **Data** | `mark` (lossless), `json`, `yaml`, `toml`, `ini`, `properties`, `xml` |
| **Documents** | `html`, `markdown`/`md`, `rst`, `org`, `wiki`, `textile`, `latex`, `jsx`, `mdx`, `text` |
| **Styles** | `css` |
| **Math** | `math-latex`, `math-ascii` (`math-typst` and `math-mathml` are stubs) |
| **Diagrams** | graph as `dot`, `mermaid` or `d2` |

Where a target lacks a concept the conversion is predictable rather than silently lossy: a `binary` value becomes a base64 string in JSON and YAML, and an element written to a data format becomes its map-and-array projection. There is no CSV writer.

---

## 6. Format Summary

| Category | Formats |
|----------|---------|
| **Lightweight markup** | Markdown (GFM/CommonMark), HTML5, reStructuredText, AsciiDoc, Wiki, Org-mode, Textile, troff/man, MDX, LaTeX, Typst, Mark |
| **Data interchange** | JSON, XML, YAML 1.2, TOML, CSV, INI, Java .properties, SQLite |
| **Document / rich text** | PDF, LaTeX (.tex); RTF experimental |
| **Personal data** | vCard (VCF), iCalendar (ICS), Email (EML / RFC 822) |
| **Diagrams / graphs** | Graphviz DOT, D2, Mermaid, Structurizr |
| **Web / code** | CSS (for Radiant), JSX, Math (LaTeX math, ASCII math) |
| **System** | Directory listing, plain text |

Every format produces a **Lambda/Mark node tree** that can be uniformly queried with `?`, transformed with pipes and `for`-expressions, validated against schemas, and exported to any of the output formats above.

---

*See also: [Doc Schema](Doc_Schema.md) · [Data & Collections](Lambda_Data.md) · [System Functions](Lambda_Sys_Func.md) · [CLI Reference](Lambda_CLI.md) · [Document Pipeline](Lambda_Doc_Pipeline.md)*
