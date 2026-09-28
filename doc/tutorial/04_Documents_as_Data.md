# 4. Documents as Data

Lambda reads JSON, CSV, YAML, Markdown, HTML and dozens of other formats into one kind of tree, built from the maps, arrays and elements of [Chapter 2](02_Values_and_Collections.md). One set of tools therefore works on all of them. In this chapter you read files in several formats, look at the tree a Markdown document becomes, query it, build new elements, and write the result out in another format.

## Reading Any Format

`input(path)` reads a file and detects its format from the extension and the first bytes. A second argument names the format outright, for a file whose name does not tell. `parse(text, format)` does the same for a string you already have:

```text repl
λ> let books = input("books.json")^
null
λ> books[0].title
"The Pragmatic Programmer"
λ> let rows = input("sales.csv", 'csv')^
null
λ> rows[1].region
"APAC"
λ> let cfg = parse("name: Ada\nlangs: [en, fr]", 'yaml')^
null
λ> cfg
{
  name: "Ada",
  langs: ["en", "fr"]
}
λ> let doc = input("notes.md")^
null
λ> doc.name
'doc'
λ> type(doc)
element
```

Both functions raise an error when the file is missing or the text does not parse, and the trailing `^` passes that error on, stopping the script with a message ([Chapter 6](06_Functions_and_Errors.md) shows how to handle it instead). Data formats such as JSON, YAML and CSV become maps and arrays; a document format such as Markdown becomes an **element**. [Markup_Formats_Support.md](../Markup_Formats_Support.md) lists every format and the tree it produces.

## The Mark Tree

To see the tree of `notes.md`, convert it to Mark, Lambda's own notation:

```bash
lambda convert notes.md -t mark -o notes.mark
```

```text
Reading input file: notes.md
Successfully parsed input file
Converting to format: mark
Writing output to: notes.mark
Conversion completed successfully!
Input:  notes.md
Output: notes.mark
Format: auto-detected → mark
```

```bash
cat notes.mark
```

The file begins like this:

```text partial
<doc version: "1.0",
  <body
    <h1 level: "1",
      "Lambda Notes">
    <p
      <span
        "Lambda treats "
        <strong
          "documents as data">
        ".">>
    <h2 level: "2",
      "Installing">
```

- The root is a `<doc>` element, and the content sits in its `<body>`.
- Elements take their HTML names: `h1`, `p`, `strong`, and further down `ul`, `li`, `a` and `code`.
- A heading records its level as the string attribute `level`. A paragraph or list item wraps its inline content in one `<span>`, and inline code is `<code type: "inline">`.

Markdown, reStructuredText, AsciiDoc, wiki markup and the other prose formats all parse into this same shape, the [Mark Doc schema](../Doc_Schema.md), so a script written against it works for all of them. An HTML page keeps its own tree instead, rooted at a `#document` element with `<html>` inside.

## Querying the Tree

`?` searches a value at every depth for items of a type. For elements the type is written like the element itself: `?<h2>` finds every `<h2>`. Save this as `queries.ls`:

```lambda
// queries.ls
let doc = input("notes.md")^
let books = input("books.json")^
count(doc?<h2>);
doc?<h2> |> ~[0];
doc?<code> |> ~[0];
(doc?<a>).href;
count(doc?<table>);
(books?{author: "Donald Knuth"}).title
```

```bash
lambda queries.ls
```

```text
3
["Installing", "First Steps", "Going Further"]
["lambda --help", "lambda", "lambda script.ls"]
"https://github.com/henry-luo/lambda"
0
"The Art of Computer Programming"
```

- A query returns `null` when nothing matches, the match **itself** when exactly one does, and a list when several do. `count` gives the number of matches in every case.
- `~[0]` is an element's first child. For a heading, a link or a piece of inline code, that child is its text, so `doc?<h2> |> ~[0]` lists the section titles.
- The document has one link, so `doc?<a>` is that `<a>` element and `.href` reads its attribute. Do not pipe a lone match: `|>` would walk into the element instead of treating it as one result.
- Queries work on data too: `?{author: "Donald Knuth"}` finds the maps whose `author` field has that value.

`?` searches all the way down. The step `[T]` looks one level down only, at the attributes and children of the value. It takes a type value: a built-in type such as `element` or `string`, or a type you name with `type` (see [Chapter 5](05_Types_and_Schemas.md)):

```lambda
// outline.ls
type Body = <body>
type Section = <h2>
let doc = input("notes.md")^
doc[Body][element] |> ~.name;
doc[Body][Section] |> ~[0];
count(doc[Section])
```

```bash
lambda outline.ls
```

```text
['h1', 'p', 'h2', 'p', 'h2', 'ul', 'h2', 'p']
["Installing", "First Steps", "Going Further"]
0
```

`doc[Body][element]` is the outline of the document, one level below `<body>`. The `<h2>` elements are children of `<body>`, not of `<doc>`, so `doc[Section]` finds none. Steps chain like XPath's `/`, and `?` is XPath's `//`.

`.?` searches like `?` but includes the value it starts from:

```text repl
λ> let doc = input("notes.md")^
null
λ> let first = (doc?<h2>)[0]
null
λ> first?<h2>
null
λ> first.?<h2> == first
true
```

## Building Elements

The literal syntax from [Chapter 2](02_Values_and_Collections.md#elements) builds documents as easily as it writes them by hand. Any expression can supply an attribute or a child, and a `for` inside an element adds one child per item:

```lambda
// shelf.ls
let books = input("books.json")^
let recent = books |: ~.year > 2000
let shelf = <ul class: "books",
    for (b in recent) <li <cite b.title> " by " b.author>
>
shelf
```

```bash
lambda shelf.ls
```

```text
<ul class: "books",
  <li
    <cite
      "Clean Code">
    " by Robert Martin">
  <li
    <cite
      "Designing Data-Intensive Applications">
    " by Martin Kleppmann">>
```

Adjacent strings in element content merge, so `" by "` and the author's name became one text child. Inside an element, `<` and `>` can also open and close tags, so a comparison written there needs parentheses — `where (b.year > 2000)` — or, as here, a filter before you build.

## Writing Other Formats

`format(value, type)` turns any value into a string in the named format: `'json'`, `'yaml'`, `'html'`, `'markdown'`, `'xml'`, `'text'` and more. Printing is an action, so this script prints from a `main` procedure and runs with `lambda run`, as in [Chapter 1](01_Getting_Started.md#scripts-that-do-things):

```lambda
// formats.ls
let book = {title: "Clean Code", year: 2008}
let para = <p "Read " <em "Clean Code"> " first.">

pn main() {
    print(format(book, 'json') ++ "\n\n")
    print(format(book, 'yaml') ++ "\n")
    print(format(para, 'markdown') ++ "\n")
    print(format(para, 'html') ++ "\n")
}
```

```bash
lambda run formats.ls
```

```text
{
  "title": "Clean Code",
  "year": 2008
}

---
# yaml formatted output
title: Clean Code
year: 2008

Read *Clean Code* first.

<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"><title>Data</title></head>
<body>
<p>Read <em>Clean Code</em> first.</p>
</body>
</html>
```

Notice the last one: the HTML formatter always produces a whole page. Unless the value is already an `<html>` element or a parsed HTML document, it is wrapped in a minimal page titled "Data". Formatting is the reverse of parsing: `format(input("notes.md")^, 'markdown')` gives back the text of `notes.md`.

## Converting from the Command Line

When all you need is the conversion itself, `lambda convert` does it without a script: `-t` names the target format, `-o` the output file, and `-f` the input format if detection is not enough. Convert the book list to YAML:

```bash
lambda convert books.json -t yaml -o books.yaml
```

```bash
cat books.yaml
```

The file begins with the first book:

```text partial
---
# yaml formatted output
title: The Pragmatic Programmer
author: Andrew Hunt
year: 1999
price: 42.5
tags:
  - craft
  - career
```

A top-level array becomes a stream of YAML documents, one per item, each starting with `---`; `input("books.yaml")` reads the stream back as an array of five maps. Markdown converts to HTML the same way:

```bash
lambda convert notes.md -t html -o notes.html
```

```bash
cat notes.html
```

```text
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"><title>Data</title></head>
<body>
<doc version="1.0"><body><h1 level="1">Lambda Notes</h1><p><span>Lambda treats <strong>documents as data</strong>.</span></p><h2 level="2">Installing</h2><p><span>Download a release and run <code type="inline">lambda --help</code>.</span></p><h2 level="2">First Steps</h2><ul><li><span>Start the REPL with <code type="inline">lambda</code>.</span></li><li><span>Run a script with <code type="inline">lambda script.ls</code>.</span></li></ul><h2 level="2">Going Further</h2><p><span>Read the <a href="https://github.com/henry-luo/lambda">reference</a>.</span></p></body></doc>
</body>
</html>
```

The converter writes the Mark Doc tree as it is — the `<doc>` wrapper, the `level` attributes and the `<span>` wrappers included — inside the same minimal page. Each `lambda convert` also prints a progress report like the one in [The Mark Tree](#the-mark-tree); it is left out here.

## Worked Example: A Table of Contents

Now put the pieces together: read `notes.md`, collect the text of its `<h2>` headings, build a `<nav>` of links to them, and write it to `toc.html`. Each link targets an anchor made from the heading, lowercased with spaces turned into dashes. Save this as `toc.ls`:

```lambda
// toc.ls
let doc = input("notes.md")^
let titles = doc?<h2> |> ~[0]
let nav = <nav class: "toc",
    <h2 "Contents">
    <ul for (t in titles) <li <a href: "#" ++ lower(replace(t, " ", "-")), t>>>
>

pn main() {
    output(nav, "toc.html")^
    print("wrote", len(titles), "links to toc.html\n")
}
```

```bash
lambda run toc.ls
```

```text
wrote 3 links to toc.html
```

```bash
cat toc.html
```

```text
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"><title>Data</title></head>
<body>
<nav class="toc"><h2>Contents</h2><ul><li><a href="#installing">Installing</a></li><li><a href="#first-steps">First Steps</a></li><li><a href="#going-further">Going Further</a></li></ul></nav>
</body>
</html>
```

- The query, the pipe and the `for` are pure expressions at the top level; only the write happens in `main`.
- `output(value, path)` picks the format from the file extension, so the `<nav>` is written as HTML, wrapped in the same minimal page as before. It raises an error if the file cannot be written, hence the `^`.
- `print` joins its arguments with spaces, which is how `3` lands between the words.

To produce a page with your own title, build the `<html>` element yourself — `<html <head <title "Contents">> <body nav>>` — and output that instead: an `<html>` root is written as it is.

## What You Learned

- `input(path)` and `parse(text, format)` read any supported format into maps, arrays and elements; `^` passes on a failure.
- Prose formats share one element tree, the Mark Doc schema; `lambda convert file -t mark` shows it.
- `?<tag>` searches every depth, `[T]` steps one level down, and `.?` includes the starting value. A single match is the value itself; `count` counts matches.
- `~[0]` is an element's first child, which for a heading or a link is its text.
- Element literals build documents, with `for` producing one child per item.
- `format(value, type)` and `lambda convert` write JSON, YAML, HTML, Markdown and more; `output` writes a file from a procedure.

[Lambda_Doc_Pipeline.md](../Lambda_Doc_Pipeline.md) describes the whole read–query–transform–write pipeline, and [Lambda_Sys_Func.md](../Lambda_Sys_Func.md#inputoutput-functions) the input and output functions. Next, [Chapter 5](05_Types_and_Schemas.md) describes the shape of data with types and checks documents against them.
