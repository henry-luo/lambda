// lambda.edit view-only projections (vibe/radiant/Radiant_Design_Edit_Mode.md
// §2): what the editor keeps but cannot edit shows as it renders, through a
// sanitizing writer — no script runs, no style reaches the page, no link
// navigates, no control acts — and a save writes the part's source.
import lambda.edit.view
import html: lambda.edit.html
import lambda.edit.model

fn shown(items) => if (len(items) == 0) "" else format(items, 'html')

"sanitized HTML:";
[shown(html_view("<div onclick=\"x()\" class=\"card\"><script>alert(1)</script><style>p{}</style>" ++
                 "<a href=\"https://example.com\" onmouseover=\"y()\">link</a> " ++
                 "<img src=\"javascript:alert(2)\" alt=\"pic\"><iframe src=\"https://example.org\"></iframe>" ++
                 "<button disabled>b</button><input value=\"v\"><!-- c --><b>bold</b></div>"))]

// a lone tag or a comment shows nothing on its own, so it has no view
"nothing to show:";
[for (s in ["<kbd>", "</kbd>", "<!-- c -->", "<span> </span>", "<script>x()</script>"]) len(html_view(s))]
"replaced elements show:";
[for (s in ["<br>", "<img src=\"a.png\">", "<input type=\"checkbox\">"]) len(html_view(s))]

// a Markdown block is sanitized whole, so raw tags pair around its text
fn md_block(src) => [for (c in content([for (c in content(parse(src, 'markdown') or null) where type(c) == element and name(c) == 'body') c][0]) where type(c) == element) c][0]
"markdown block:";
[shown(markdown_view(md_block("Press <kbd>K</kbd>, see [docs](https://example.com) and ![p](p.png)[^1].\n")))]
"markdown block with raw script:";
[shown(markdown_view(md_block("Hi <script>alert(1)</script> there.\n")))]

// math reads as its TeX source
"markdown block with math:";
[shown(markdown_view(md_block("Area $\\pi r^2$ & more.\n"))), shown(markdown_view(md_block("$$\nx < y\n$$\n")))]

// the HTML adapter keeps an element outside its profile with a view; one
// that shows nothing keeps only its source
let loaded = html.import_text("<p>Press <kbd>K</kbd> <a name=\"top\"></a></p><video src=\"v.mp4\"></video>\n") or null
let kept = [for (b in loaded.doc.content) for (x in [b, *b.content] where x.kind == 'node' and (x.tag == 'raw_html' or x.tag == 'html_block')) x];
"html adapter views:";
[for (k in kept) [k.tag, shown(attr_get(k, view_attr))]]
"views stay out of the saved HTML:";
[html.export_text(loaded.doc, loaded.envelope)]
