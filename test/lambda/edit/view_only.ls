// lambda.edit view-only projections (vibe/radiant/Radiant_Design_Edit_Mode.md
// §2): what the editor keeps but cannot edit shows as it renders, through a
// sanitizing writer — no script runs, no style reaches the page, no link
// navigates, no control acts — and a save writes the part's source.
import lambda.edit.view
import html: lambda.edit.html
import md: lambda.edit.markdown
import lambda.edit.model

fn shown(items) => if (len(items) == 0) "" else format(items, 'html')
fn has_math(items) {
  let markup = shown(items)
  contains(markup, "lambda-math") and contains(markup, "<text") and contains(markup, "@font-face")
}

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

// inline, display and retained Markdown blocks all use the typesetter;
// projections never leak into saved source.
"markdown math renders:";
[for (source in ["Area $\\pi r^2$ & more.\n", "$$\nx < y\n$$\n",
                 "Fraction $\\frac{1}{2}$ and root $\\sqrt{x}$.[^1]\n\n[^1]: a note\n"])
  has_math(markdown_view(md_block(source)))]
let math_source = "Inline $\\frac{1}{2}$ and $\\sqrt{x}$.\n\n$$\nE = mc^2\n$$\n"
let math_doc = md.import_text(math_source) or null
let formulas = [for (n in math_doc.doc.content[0].content where n.tag == 'math') n];
"inline math projections:";
[for (n in formulas) has_math(attr_get(n, view_attr))]
"math source survives:";
[md.export_text(math_doc.doc, math_doc.envelope) == math_source,
 md.check_roundtrip(math_doc.doc, math_doc.envelope) == null]

// the HTML adapter keeps an element outside its profile with a view; one
// that shows nothing keeps only its source
let loaded = html.import_text("<p>Press <kbd>K</kbd> <a name=\"top\"></a></p><video src=\"v.mp4\"></video>\n") or null
let kept = [for (b in loaded.doc.content) for (x in [b, *b.content] where x.kind == 'node' and (x.tag == 'raw_html' or x.tag == 'html_block')) x];
"html adapter views:";
[for (k in kept) [k.tag, shown(attr_get(k, view_attr))]]
"views stay out of the saved HTML:";
[html.export_text(loaded.doc, loaded.envelope)]
