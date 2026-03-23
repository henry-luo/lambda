# Markup & Data Format Support

Lambda parses a wide variety of text and binary formats into a single, uniform in-memory
representation — the **Lambda/Mark node tree** — so that all downstream transformation,
validation, layout and rendering code works on the same data model regardless of the
original source format.

```
[Source file]  →  [parser]  →  [Lambda/Mark tree]  →  [transform / validate / render]
```

This document covers three groups of supported formats:

1. [Lightweight markup languages](#1-lightweight-markup-languages) — human-authored prose that maps to the **Mark Doc** element schema
2. [Data-interchange formats](#2-data-interchange-formats) — structured data that maps to Lambda **maps, arrays, and elements**
3. [Other formats](#3-other-formats) — PDF, email, calendars, graphs, CSS, and more

---

## 1. Lightweight Markup Languages

All lightweight markup flavors (Markdown, reStructuredText, AsciiDoc, …) are parsed
into the same **Mark Doc schema** — an element tree rooted at `<doc>`.
Block structure (headings, paragraphs, lists, code blocks, tables) and inlines (emphasis,
links, code spans) are mapped to HTML-compatible element names where an HTML equivalent
exists, with a small set of custom `<mark>` elements for features that have no direct
HTML analogue (math, citations, footnotes).
See [Doc Schema](Doc_Schema.md) for the full schema reference.

### 1.1 Supported Flavors

| Format | Input type string | Notes |
|--------|:-----------------:|-------|
| CommonMark / GitHub Flavored Markdown | `markdown` | 100 % CommonMark test-suite pass rate |
| reStructuredText | `rst` | |
| MediaWiki / DokuWiki markup | `wiki` | |
| AsciiDoc | `asciidoc` / `adoc` | |
| Emacs Org-mode | `org` | |
| Textile | `textile` | |
| troff/man pages | `man` | |
| MDX (Markdown + JSX) | `mdx` | inlines React/JSX components |
| LaTeX | `latex` | see also §3 |
| HTML5 | `html` / `html5` | 100 % html5lib test-suite pass rate |

In Lambda scripts all of these are loaded with `input()`:

```lambda
let md   = input("readme.md",    'markdown')
let rst  = input("design.rst",   'rst')
let wiki = input("page.wiki",    'wiki')
let adoc = input("guide.adoc",   'asciidoc')
let html = input("index.html",   'html')
```

### 1.2 Unified Mark Doc Schema

Every markup parser produces the same element-tree shape.
Common block and inline element names are intentionally aligned with HTML so they pass
directly to the Radiant layout engine without a translation step.

| Category | Mark element(s) | Notes |
|----------|-----------------|-------|
| Document root | `<doc>` | carries `<meta>` as first child |
| Document metadata | `<meta title: … author: … date: …>` | unified across all flavors |
| Headings | `<h1>` … `<h6>` | |
| Paragraph | `<p>` | |
| Block quote | `<blockquote>` | |
| Code block | `<pre><code class:"language-X" …>` | |
| Horizontal rule | `<hr>` | |
| Unordered list / item | `<ul>` / `<li>` | |
| Ordered list / item | `<ol start:N>` / `<li>` | |
| Definition list | `<dl>` / `<dt>` / `<dd>` | |
| Table | `<table>` / `<thead>` / `<tbody>` / `<tr>` / `<th>` / `<td>` | |
| Figure / caption | `<figure>` / `<figcaption>` | |
| Bold / strong | `<strong>` | |
| Italic / emphasis | `<em>` | |
| Inline code | `<code>` | |
| Hyperlink | `<a href:…>` | |
| Image | `<img src:… alt:…>` | |
| Line break | `<br>` | |
| Math (inline) | `<math mode:'inline' …>` | custom Mark element |
| Math (display) | `<math mode:'display' …>` | custom Mark element |
| Footnote | `<footnote id:…>` | custom Mark element |
| Citation | `<cite keys:[…]>` | custom Mark element |
| Raw pass-through | `<raw format:'html' …>` | custom Mark element |

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
| Bob   | 25  |
```

</td>
<td>

```mark
<doc
  <meta>
  <h1 "Hello World">
  <p
    "Some "
    <strong "bold">
    " and "
    <em "italic">
    " text with a "
    <a href:"https://example.com" "link">
    "."
  >
  <ul
    <li "item one">
    <li "item two">
  >
  <table
    <thead
      <tr <th "Name"> <th "Age">>
    >
    <tbody
      <tr <td "Alice"> <td "30">>
      <tr <td "Bob">   <td "25">>
    >
  >
>
```

</td>
</tr>
</tbody>
</table>

### 1.4 HTML → Mark Doc

HTML5 is a first-class input format. The tree-structure is preserved verbatim; the
`<doc>` wrapper is added for schema uniformity.

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
<doc
  <html lang:"en"
    <head <title "Page">>
    <body
      <h1 class:"title" "Hello">
      <p "A " <em "simple"> " page.">
    >
  >
>
```

</td>
</tr>
</tbody>
</table>

---

## 2. Data-Interchange Formats

Structured data formats (JSON, XML, YAML, TOML, CSV, …) map to Lambda's **map** (`{…}`),
**array** (`[…]`), and **element** (`<tag …>`) literals.
Once parsed you have a live Lambda value that you can query, transform, and export to any
other format with a single pipeline.

### 2.1 Supported Formats

| Format | Input type string | Lambda data model |
|--------|:-----------------:|:-----------------:|
| JSON | `json` | maps + arrays |
| XML | `xml` | element tree |
| YAML 1.2 | `yaml` | maps + arrays |
| TOML | `toml` | maps + arrays |
| CSV | `csv` | array of maps |
| INI | `ini` | nested maps |
| Java `.properties` | `properties` | flat map |
| Key-value pairs | `kv` | flat map |

### 2.2 JSON → Lambda Map / Array

JSON is the closest format to Lambda's native literal syntax.
Object keys become unquoted map keys; value types are preserved.

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
let data = input("users.json", 'json')
data.name               // "Alice"
data.tags[0]            // "dev"
data.address.city       // "New York"
```

### 2.3 XML → Lambda Element Tree

XML documents map to Lambda **elements** — named nodes that carry both attributes (as
key-value pairs) and an ordered list of children (text nodes or nested elements).
This mirrors Mark Notation exactly.

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
  <book id="2" lang="en">
    <title>The Pragmatic Programmer</title>
    <author>Hunt &amp; Thomas</author>
    <year>1999</year>
  </book>
</library>
```

</td>
<td>

```mark
<library
  <book id:1 lang:"en"
    <title "Clean Code">
    <author "Robert Martin">
    <year "2008">
  >
  <book id:2 lang:"en"
    <title "The Pragmatic Programmer">
    <author "Hunt & Thomas">
    <year "1999">
  >
>
```

</td>
</tr>
</tbody>
</table>

**Querying with the `?` operator:**

```lambda
let lib = input("library.xml", 'xml')
lib?<book>              // all book elements
lib?<book lang:"en">    // books where lang == "en"
lib?<book> | ~.title    // ["Clean Code", "The Pragmatic Programmer"]
```

### 2.4 YAML → Lambda Map / Array

YAML documents (including multi-document streams) map to the same map/array model as JSON.
All YAML scalar types (strings, ints, floats, booleans, nulls, timestamps) are converted
to their Lambda equivalents.

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

TOML section headers become nested map keys; dotted keys are expanded into nested maps.

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

The first row is treated as a header and becomes the map keys for every subsequent row.
Values are kept as strings unless numeric coercion is explicitly requested.

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
  {name:"Alice", age:"30", city:"New York",     score:"9.5"},
  {name:"Bob",   age:"25", city:"Los Angeles",  score:"8.2"},
  {name:"Carol", age:"35", city:"Chicago",      score:"9.8"}
]
```

</td>
</tr>
</tbody>
</table>

**Typical processing pipeline:**

```lambda
let rows = input("data.csv", 'csv')

// filter and project
for (r in rows where num(r.age) >= 30)
  {name: r.name, city: r.city}
```

### 2.7 INI / .properties / Key-Value

Flat or lightly-nested configuration formats map to one or two levels of maps.

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
    port: "5432",
    name: "mydb"
  },
  cache: {
    host: "localhost",
    port: "6379",
    ttl:  "300"
  }
}
```

</td>
</tr>
</tbody>
</table>

`.properties` files (no sections) become a flat map:

```lambda
// app.properties:  app.name=MyApp\napp.version=2.0
let cfg = input("app.properties", 'properties')
cfg."app.name"    // "MyApp"
```

### 2.8 Format Conversion

Any two supported data formats can be round-tripped through Lambda's `format()` function
or the `lambda convert` CLI command:

```lambda
// In a Lambda script
let data = input("config.yaml", 'yaml')
format(data, 'json')          // → JSON string
format(data, 'toml')          // → TOML string
```

```bash
# CLI
lambda convert config.yaml -t json -o config.json
lambda convert data.csv  -t yaml -o data.yaml
lambda convert page.md   -t html -o page.html
```

---

## 3. Other Formats

### 3.1 LaTeX

LaTeX source (`.tex`) is parsed by a Tree-sitter–based parser into an element tree
closely following the Mark Doc schema, with custom elements for LaTeX-specific constructs
(`<math>`, `<cite>`, `<env name:…>`, `<cmd name:…>`).
The `lambda view` command renders `.tex` files by first converting them to HTML.

```lambda
let doc = input("paper.tex", 'latex')
format(doc, 'html')           // convert to HTML
```

### 3.2 PDF

PDF documents are parsed into a Mark element tree with best-effort text flow
reconstruction. Binary streams inside the PDF are decompressed before parsing.

```lambda
let report = input("annual_report.pdf", 'pdf')
report?<p> | ~[0]             // first paragraph text
```

### 3.3 RTF

Rich Text Format documents are parsed into the same Mark Doc element schema, preserving
text runs, paragraph styles, and basic table structure.

```lambda
let doc = input("letter.rtf", 'rtf')
```

### 3.4 Email (EML / RFC 822)

E-mail files are parsed into a map with well-known header fields and a content body.
MIME multipart messages yield the body as an array of parts.

<table>
<thead>
<tr><th>EML source (excerpt)</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```
From: alice@example.com
To: bob@example.com
Subject: Hello
Date: Mon, 1 Jan 2024 10:00:00 +0000
MIME-Version: 1.0
Content-Type: text/plain

Hi Bob, just checking in.
```

</td>
<td>

```lambda
{
  from:    "alice@example.com",
  to:      "bob@example.com",
  subject: "Hello",
  date:    t'2024-01-01T10:00:00Z',
  body:    "Hi Bob, just checking in."
}
```

</td>
</tr>
</tbody>
</table>

### 3.5 vCard (VCF)

Contact cards are parsed into maps following the vCard 3.0 / 4.0 property names.

```lambda
let contact = input("alice.vcf", 'vcf')
contact.fn        // "Alice Wonderland"
contact.email     // "alice@example.com"
contact.tel       // "+1-555-0100"
```

### 3.6 iCalendar (ICS)

Calendar files are parsed into a map with a `vcalendar` root and an array of `vevent`,
`vtodo`, and `vjournal` components.

```lambda
let cal = input("events.ics", 'ics')
cal.vcalendar.vevent | ~.summary    // ["Team standup", "Sprint review", …]
```

### 3.7 Graph Formats

Diagram description languages are parsed into a graph element tree
(`<graph>` containing `<node>` and `<edge>` elements).
Three flavors are supported via the `graph` parser type:

| Flavor | Input type | File extension |
|--------|:----------:|:--------------:|
| Graphviz DOT | `graph` / `dot` | `.dot`, `.gv` |
| D2 diagrams | `graph` / `d2` | `.d2` |
| Mermaid diagrams | `graph` / `mermaid` | `.mmd` |

```lambda
let g = input("arch.dot", 'graph')
g?<node> | ~.id          // list all node IDs
g?<edge src:"a">         // edges leaving node "a"
```

### 3.8 CSS

CSS stylesheets are parsed into a rule list — an array of maps with `selector` and
`declarations` fields.

<table>
<thead>
<tr><th>CSS source</th><th>Lambda / Mark</th></tr>
</thead>
<tbody>
<tr>
<td>

```css
body {
  font-family: sans-serif;
  margin: 0;
}

h1, h2 {
  color: #333;
  font-weight: bold;
}
```

</td>
<td>

```lambda
[
  {
    selector: "body",
    declarations: {
      "font-family": "sans-serif",
      margin: "0"
    }
  },
  {
    selector: "h1, h2",
    declarations: {
      color: "#333",
      "font-weight": "bold"
    }
  }
]
```

</td>
</tr>
</tbody>
</table>

### 3.9 JSX / MDX

JSX and MDX files interleave markup with code expressions.
They are parsed into element trees where JSX component invocations become elements with
the component name preserved as the tag, and `{expression}` slots are captured as
`<expr>` children.

```lambda
let page = input("Page.mdx", 'mdx')
page?<Card>               // all <Card> component usages
```

### 3.10 Math

Mathematical notation can be parsed standalone from LaTeX math or AsciiMath sources into
a `<math>` element tree:

```lambda
let expr = input("formula.tex", 'math')   // math-only LaTeX
let expr = input("formula.asc", 'math-ascii')
```

### 3.11 Directory Listing

A local directory path can be treated as an input, producing an array of file-info maps:

```lambda
let files = input("./src", 'dir')
files that ~.ext == ".cpp" | ~.name     // list all .cpp filenames
```

---

## 4. Using the Input Function

All formats are accessed through the same `input()` built-in:

```lambda
// Explicit type
let data = input("file.ext", 'format')

// Auto-detect from MIME / file extension
let data = input("data.json")

// From a URL (HTTP/HTTPS)
let data = input("https://api.example.com/data.json")

// From a string in memory
let data = input_str(raw_string, 'yaml')
```

**CLI equivalent — `lambda convert`:**

```bash
lambda convert input.yaml  -t json   -o output.json
lambda convert input.md    -t html   -o output.html
lambda convert input.csv   -t yaml   -o output.yaml
```

**MIME auto-detection** is built in: when no type string is given, Lambda inspects the
file header bytes and extension to select the right parser automatically.

---

## 5. Format Summary

| Category | Formats |
|----------|---------|
| **Lightweight markup** | Markdown (GFM), HTML5, reStructuredText, AsciiDoc, Wiki, Org-mode, Textile, troff/man, MDX, LaTeX |
| **Data interchange** | JSON, XML, YAML 1.2, TOML, CSV, INI, Java .properties, key-value |
| **Document / rich text** | PDF, RTF, LaTeX (.tex) |
| **Personal data** | vCard (VCF), iCalendar (ICS), Email (EML / RFC 822) |
| **Diagrams / graphs** | Graphviz DOT, D2, Mermaid |
| **Web / code** | CSS, JSX, Math (LaTeX math, AsciiMath) |
| **System** | Directory listing, plain text |

All formats produce a **Lambda/Mark node tree** that can be uniformly queried with `?`,
transformed with pipes and `for`-expressions, validated against schemas, and exported to
any other supported output format.

---

*See also: [Doc Schema](Doc_Schema.md) · [Data & Collections](Lambda_Data.md) · [System Functions](Lambda_Sys_Func.md) · [CLI Reference](Lambda_CLI.md)*
