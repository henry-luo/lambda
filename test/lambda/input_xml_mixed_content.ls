// XML reader and writer fidelity. Character data in mixed content (text
// beside child elements) is kept as written, since its spaces separate words;
// white space between the elements of element-only content and the edges of a
// text-only element are not significant. A DOCTYPE's internal subset ends at
// its `]`. The writer spells comments and processing instructions as such.
fn xml(src) => parse(src, 'xml') or null
fn root(src) => [for (c in content(xml(src)) where type(c) == element and not starts_with(string(name(c)), "?") and not starts_with(string(name(c)), "!")) c][0]

"mixed content keeps its spaces:";
[for (c in content(root("<p>Hello <b>big</b> world <i>!</i></p>"))) c];
"white space between elements is layout:";
len(content(root("<list>\n  <item>a</item>\n  <item>b</item>\n</list>")));
"a text-only element is trimmed:";
[content(root("<name>\n    Alice\n  </name>"))[0]];
"CDATA stays as written:";
[content(root("<code><![CDATA[  a < b  ]]></code>"))[0]];
"a DOCTYPE internal subset ends at its bracket:";
[for (c in content(xml("<?xml version=\"1.0\"?>\n<!DOCTYPE r [\n  <!ENTITY e \"x\">\n]>\n<r>text</r>\n<!-- after -->\n"))) name(c)];
"comments and processing instructions are written as such:";
[format(xml("<?xml version=\"1.0\"?>\n<!-- note --><?pi data?><r>x</r>"), 'xml') or "(error)"]
