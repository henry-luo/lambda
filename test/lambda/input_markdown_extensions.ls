// Markdown dialect policy: vibe/input/Input_Markdown.md; Input-owned nodes follow D7.1.5.
let source = "First[^a], second[^b], repeated[^a], missing[^missing].\n\n[^b]: A nested note[^inner].\n\n[^a]: **Rich** text with [site][target].\n\n    Another paragraph.\n\n    - one\n    - two\n\n[^inner]: Nested content.\n\n[^unused]: Kept for formatting.\n\n[target]: https://example.com\n\n```text\n[^fake]: Not a definition.\n```\n\n<div>\n[^html]: Not a definition.\n</div>\n"
let document = parse(source, 'markdown')^
let references = [for (node in document?element where name(node) == 'footnote-ref') node]
let notes = document?<footnote>
let written = format(document, 'markdown')^
let rewritten = format(parse(written, 'markdown')^, 'markdown')^;

// Forward/repeated references, note ordering, unresolved text, and rich blocks.
[for (reference in references) reference.ref];
[for (reference in references) reference.number];
[for (note in notes) note.label];
count(document?<strong>) == 1;
count(document?<li>) == 2;
count(document?<a>) == 5;
notes[0].id == "fn-1";
contains(document?string, "[^missing]");
contains(written, "[^unused]: Kept for formatting.");
written == rewritten;

// GFM autolinks respect boundaries, code, explicit links, punctuation and parentheses.
let links = parse("www.example.com https://example.com/a_(b)). user+tag@example.com. `www.code.example` [www.label.example](https://target.example) :https://not.example\n", 'markdown')^?<a>;
[for (link in links) link.href];
count(parse("```\nwww.code.example\n```\n", 'markdown')^?<a>) == 0;
count(parse("www.example.com\n\n[^a]: note\n", {type:"markup", flavor:"commonmark"})^?<footnote>) == 0;
count(parse("www.example.com", {type:"markup", flavor:"commonmark"})^?<a>) == 0;

// Mermaid remains source-preserving until presentation; HTML has no GFM tag filter.
let fenced = parse("```mermaid\nflowchart LR\n  A --> B\n```\n", 'markdown')^;
(fenced?<code>).language == "mermaid";
contains(format(fenced, 'markdown')^, "flowchart LR");
let html = parse("<script>kept()</script>\n\nInline <iframe src=\"about:blank\"></iframe>.\n", 'markdown')^
contains(format(html, 'markdown')^, "<script>kept()</script>");
contains(format(html, 'markdown')^, "<iframe src=\"about:blank\">");

// Definitions in containers share document scope; first normalized label wins.
let nested = parse("Quoted[^Q] and listed[^L].\n\n> [^q]: Quote **note**.\n\n- item\n\n  [^l]: List note.\n\n[^Q]: Ignored duplicate.\n", 'markdown')^
let nested_notes = nested?<footnote>;
[for (note in nested_notes) note.label];
count(nested?<strong>) == 1;
not contains(format(nested, 'markdown')^, "Ignored duplicate");

// A reference cycle resolves each definition once and keeps stable numbering.
let cycle = parse("Start[^a].\n\n[^a]: First[^b].\n[^b]: Second[^a].\n", 'markdown')^
let cycle_refs = [for (node in cycle?element where name(node) == 'footnote-ref') node];
[for (node in cycle_refs) node.number];
count(cycle?<footnote>) == 2;

// Unused definitions also retain linked content and literal unresolved markers.
let unused = parse("[^unused]: See[^leaf] and [^missing].\n\n[^leaf]: Leaf.\n", 'markdown')^
let unused_refs = [for (node in unused?element where name(node) == 'footnote-ref') node];
[for (node in unused_refs) node.number];
contains(unused?string, "[^missing]");
count(unused?<footnote>) == 2
