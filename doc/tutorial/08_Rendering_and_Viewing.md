# 8. Rendering and Viewing

Lambda carries its own browser engine, Radiant. It resolves CSS, lays out boxes and paints them, into a window or into a file. In this chapter you lay out an HTML page, render it to SVG, PDF and PNG, turn a Lambda script into a page, render Markdown and a diagram, and open documents in the viewer.

## A Page to Render

Save this page as `page.html`. It uses a web-safe font, borders with rounded corners and a flexbox row of cards, and no images:

```html file=page.html
<!DOCTYPE html>
<html>
<head>
<title>Reading List</title>
<style>
  body { font-family: Georgia, serif; margin: 24px; color: #222; }
  h1 { font-size: 24px; margin: 0 0 16px; }
  .cards { display: flex; gap: 12px; }
  .card { flex: 1; border: 2px solid #4a6fa5; border-radius: 8px; padding: 12px; background: #f4f7fb; }
  .card h2 { font-size: 16px; margin: 0 0 4px; }
  .card p { font-size: 13px; margin: 0; color: #555; }
</style>
</head>
<body>
<h1>Reading List</h1>
<div class="cards">
  <div class="card"><h2>Clean Code</h2><p>Robert Martin, 2008</p></div>
  <div class="card"><h2>SICP</h2><p>Harold Abelson, 1985</p></div>
  <div class="card"><h2>TAOCP</h2><p>Donald Knuth, 1968</p></div>
</div>
</body>
</html>
```

## Laying Out a Page

`lambda layout` runs the CSS cascade and the layout algorithms and records the result: one box for every element and every run of text, with its position and size. On the terminal it prints only a summary line; `--view-output` names the file that receives the box tree:

```bash
lambda layout page.html --view-output layout.json
```

```text
Completed layout command: 1 success, 0 failed
```

The box tree is JSON, and it starts at the root element:

```bash
cat layout.json
```

```text partial
  "layout_tree":   {
    "type": "block",
    "tag": "html",
    "selector": "html",
    "classes": [],
    "layout": {
      "x": 0.0,
      "y": 0.0,
      "width": 1200.0,
```

Each box has a `type` (`block`, `inline`, `list-item`, `text`), the element's `tag`, a CSS `selector` that identifies it, and a `layout` rectangle in CSS pixels, measured from the top-left corner of the page. After these come `computed`, the resolved style (display, margins, padding, font), and `children`, the boxes inside. The page was laid out in the default viewport, 1200 pixels wide; `-vw` and `-vh` choose another size.

Because the box tree is JSON, Lambda can query it like any other document. Save this as `boxes.ls`:

```lambda
// boxes.ls
let tree = input("layout.json")^
for (box in tree?{tag: "div"}) [box.selector, box.layout.x, box.layout.width]
```

```bash
lambda boxes.ls
```

```text
["div.cards", 24, 1152]
["div.card:nth-of-type(1)", 24, 376]
["div.card:nth-of-type(2)", 412, 376]
["div.card:nth-of-type(3)", 800, 376]
```

`tree?{tag: "div"}` finds every map in the tree whose `tag` field is `"div"`, at any depth: the same `?` query you used on HTML in [Chapter 4](04_Documents_as_Data.md), with a map type as the pattern. The numbers show flexbox at work. The body's 24-pixel margins leave a row 1152 pixels wide, and `flex: 1` shares it equally among the three cards once the two 12-pixel gaps are taken out: (1152 − 24) / 3 = 376.

## Rendering to SVG, PDF and PNG

`lambda render` lays the page out and paints it into a file whose extension picks the format:

```bash
lambda render page.html -o page.svg
```

`render` prints nothing when it succeeds; open `page.svg` in a web browser to see the page. SVG output keeps text as text, so the result is itself a document that you can search, or read back with Lambda. Save this as `svg_text.ls`:

```lambda
// svg_text.ls
[for (t in input("page.svg")^?<text>) t[0]]
```

```bash
lambda svg_text.ls
```

```text
["Reading List", "Clean Code", "Robert Martin, 2008", "SICP", "Harold Abelson, 1985", "TAOCP", "Donald Knuth, 1968"]
```

A `.pdf` extension writes a PDF, and `.png` or `.jpg` a bitmap image:

```bash
lambda render page.html -o page.pdf
```

```bash
lambda render page.html -o page.png -vw 600 --scale 2
```

Three options control the size of the result:

| Option | Effect |
|---|---|
| `-vw 600` | Lay the page out 600 CSS pixels wide. The default is 1200, or 800 for PDF |
| `-vh 400` | Set the viewport height. Without it, the output grows to fit the content |
| `--scale 2` | Write two pixels per CSS pixel, for sharper PNG and JPEG images; the layout does not change |

In the 600-pixel viewport the three cards shrink to share the narrower row, and `--scale 2` doubles the image in each direction. PDF output currently draws text with the standard PDF fonts (Times for a `Times` family, Courier for `Courier`, Helvetica for everything else), so prefer SVG or PNG when the typeface matters.

## A Script as a Page

A Lambda script whose result is an `<html>` element *is* a page: `layout`, `render` and `view` accept it wherever they accept an HTML file. This one turns `books.json` into a table, oldest book first. Save it as `page.ls`:

```lambda
// page.ls
let books = input("books.json")^
let css = "body { font-family: Georgia, serif; margin: 24px; color: #222; }
    table { border-collapse: collapse; }
    th, td { border: 1px solid #999; padding: 4px 10px; text-align: left; }
    th { background: #e8eef6; }";

<html
    <head <title "Books"> <style css>>
    <body
        <h1 "Books">
        <table
            <tr <th "Title"> <th "Author"> <th "Year"> <th "Tags">>
            for (b in books order by b.year)
                <tr <td b.title> <td b.author> <td string(b.year)> <td join(b.tags, ", ")>>
        >
    >
>
```

The stylesheet is an ordinary string placed inside `<style>`, and the `for` expression inside `<table>` produces one `<tr>` per book, which become the table's rows. `string(b.year)` turns the year into text: the renderer currently shows only string content, so a bare number, as in `<td b.year>`, would leave its cell empty. Run the file as an ordinary script and it prints the element it builds:

```bash
lambda page.ls
```

```text partial
      <tr
        <td
          "The Art of Computer Programming">
        <td
          "Donald Knuth">
        <td
          "1968">
```

Render it exactly like the HTML page:

```bash
lambda render page.ls -o books.svg
```

Reading `books.svg` back finds 25 runs of text: the heading, four column headers and four cells for each of the five books.

```lambda
// books_text.ls
count(input("books.svg")^?<text>)
```

```bash
lambda books_text.ls
```

```text
25
```

A script whose result is some other element, a lone `<table>` for instance, is placed in the body of an otherwise empty page.

## Markdown and Diagrams

`render` takes other document formats too. A Markdown file is parsed into the document tree of [Chapter 4](04_Documents_as_Data.md) and rendered with default styles:

```bash
lambda render notes.md -o notes.pdf
```

Diagrams written in Mermaid, D2 or Graphviz DOT are laid out by the bundled `graph` package. Save this flowchart as `flow.mmd`:

```mmd file=flow.mmd
flowchart LR
    A[books.json] --> B(Lambda script)
    B --> C{Output}
    C -->|convert| D[JSON / YAML]
    C -->|render| E[SVG / PDF / PNG]
```

```bash
lambda render flow.mmd -o flow.svg
```

The node and edge labels are real text in the SVG as well:

```lambda
// flow_text.ls
[for (t in input("flow.svg")^?<text>) t[0]]
```

```bash
lambda flow_text.ls
```

```text
["books.json", "Lambda script", "Output", "JSON / YAML", "SVG / PDF / PNG", "convert", "render"]
```

The diagram comes out in a light palette; `--theme tokyo-night` selects a dark one. Since the graph layout is itself a Lambda package, this command needs the runtime's package folder: if it fails when you work outside the Lambda folder, set `LAMBDA_HOME` as [Chapter 1](01_Getting_Started.md#get-lambda) describes.

## The Viewer

```bash
lambda view page.html
```

`lambda view` opens a document in a window, drawn by the same engine that `render` uses. You can scroll, select and copy text, follow links and fill in forms, and the page's JavaScript runs on Lambda's own JavaScript engine. The viewer also opens Markdown, PDF, diagrams, images, data files, Lambda scripts and `https://` URLs: `lambda view page.ls` shows the book table, and `lambda view notes.md` the notes. Press Esc to close the window.

## What You Learned

- `lambda layout` computes the box tree, with every box's position, size and resolved style, as JSON that you can query.
- `lambda render file -o out` paints to SVG, PDF, PNG or JPEG, chosen by the extension; `-vw` and `-vh` set the viewport and `--scale` the pixel density.
- A script whose result is an element is a page; give it string content to render.
- Markdown files and Mermaid, D2 and DOT diagrams render the same way.
- `lambda view` opens any of them in an interactive window.

[HTML_CSS_SVG_Support.md](../HTML_CSS_SVG_Support.md) lists the HTML, CSS and SVG features Radiant supports, [Lambda_CLI.md](../Lambda_CLI.md#render--render-to-image-or-document) every option of `render`, `layout` and `view`, and [Lambda_Doc_Pipeline.md](../Lambda_Doc_Pipeline.md) how rendering fits into the rest of Lambda. Next, [Chapter 9](09_Reactive_UI.md) makes a page interactive.
