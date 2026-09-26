// lambda.edit HTML adapter (vibe/radiant/Radiant_Design_Edit_Mode.md §5 HTML,
// §8): the parsed head, doctype and attributes are an envelope written back
// as read; the body's flow content is the editor model; elements outside the
// profile are retained whole; the round-trip gate guards open and save.
import html: lambda.edit.html
import lambda.edit.model
import lambda.editor.mod_doc

fn load(src) => html.import_text(src) ^ { {error: ^.message} }
fn written(src) {
  let l = load(src)
  if (l.error != null) l.error else html.export_text(l.doc, l.envelope)
}
fn tags(src) => [for (b in load(src).doc.content) b.tag]
// writing, reading, and writing again changes nothing
fn stable(src) => written(written(src)) == written(src)
fn gate(src) {
  let l = load(src)
  html.check_roundtrip(l.doc, l.envelope)
}

"fragment:";
[written("<p>One</p>\n<p>Two <b>bold</b></p>\n")];
"full document keeps doctype, head and attributes:";
[written("<!DOCTYPE html>\n<html lang=\"fr\"><head><title>T</title><style>p{}</style></head><body id=\"b\"><p>x</p></body></html>")];
"legacy doctype identifiers:";
[written("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\"><html><body><p>x</p></body></html>")];

// a container's lone run of text is an implied paragraph, written back bare
"implied paragraph:";
[tags("<div class=\"n\">Hi <i>there</i></div>"), written("<div class=\"n\">Hi <i>there</i></div>")];
// a container mixing runs and blocks is kept whole
"mixed container kept:";
[tags("<div>text <p>para</p> more</div>"), written("<div>text <p>para</p> more</div>")];
// at the root, the runs are kept and the blocks stay editable
"mixed body:";
[tags("<p>a</p>stray <b>text</b><p>b</p>"), written("<p>a</p>stray <b>text</b><p>b</p>")];

"marks with attributes, icons, anchors, retained inline:";
[written("<p><span class=\"hl\">x</span> <i class=\"icon\"></i> <a id=\"top\"></a> <b>k <kbd>K</kbd></b></p>")];
"white space settles as rendered:";
[written("<p>\n  a   b\n  <b> c </b>  d\n</p>")];
"preformatted text is exact, a leading newline doubled:";
[written("<pre>\n\nx  y\n</pre>"), written("<pre><code class=\"language-c\">int  x;</code></pre>")];
"list items mix runs and blocks:";
[written("<ul><li>Item<ul><li>sub</li></ul></li><li></li></ul>")];
"a table with block cells is kept whole:";
[tags("<table><tr><td><p>x</p></td></tr></table>"), tags("<table><tr><td>x</td></tr></table>")];

"every sample is stable:";
[for (s in ["<p>One</p>", "<div class=\"n\">Hi <i>there</i></div>", "<div>text <p>p</p> more</div>",
            "<p>a</p>stray <b>text</b><p>b</p>", "<ul><li>Item<ul><li>sub</li></ul></li></ul>",
            "<pre>\n\nx</pre>", "<!DOCTYPE html><html><head><title>T</title></head><body><p>x</p></body></html>"])
   stable(s)];
"the gate passes on loaded documents:";
[gate("<p>One</p>") == null, gate("<div>text <p>p</p> more</div>") == null];

// the gate names the first block a save would change
let l = load("<p>x</p>")
let broken = node('doc', [node_attrs('p', [], [text_marked("y", [{name: 'q', value: true}])])])
"gate on an unrepresentable mark:"; [html.check_roundtrip(broken, l.envelope)];
"XHTML refused:"; [load("<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\"></html>").error]
