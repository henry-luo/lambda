// Markdown emoji policy: vibe/input/Input_Markdown.md; Input-owned nodes follow D7.1.5.
import markdown: lambda.edit.markdown

let catalog = input("lib/emoji_shortcodes_data.json", 'json')^
let aliases = [*[for (alias at catalog.unicode) alias], *[for (alias at catalog.images) alias]]
let source = join([for (alias in aliases) ":" ++ alias ++ ":"], " ") ++ "\n"
let document = parse(source, 'markdown')^
count(document?symbol);
count(document?<img>);

// Each alias must produce its own upstream Unicode value, including complete sequences.
all([for (alias, expected at catalog.unicode) {
  let parsed = parse(":" ++ alias ++ ":", 'markdown')^
  let value = parsed?symbol;
  type(value) == symbol and format([value], 'html')^ == expected
}]);

// Every custom alias is an inline image and retains its shortcode when saved.
all([for (alias, url at catalog.images) {
  let parsed = parse(":" ++ alias ++ ":", 'markdown')^
  let image = parsed?<img>
  name(image) == 'img' and image.src == url and image["data-emoji"] == string(alias) and
    image.alt == ":" ++ alias ++ ":" and contains(image.style, "width:1em") and
    contains(format(parsed, 'html')^, url) and
    format(parsed, 'markdown')^ == ":" ++ alias ++ ":\n"
}]);
format(document, 'markdown')^ == source;

// Legacy Lambda aliases remain available alongside the upstream catalog.
all([for (alias, expected at catalog.legacy) {
  let parsed = parse(":" ++ alias ++ ":", 'markdown')^;
  format([parsed?symbol], 'html')^ == expected
}]);

// Escaped literal shortcodes stay literal after repeated saves, including split text nodes.
let literal = "\\:smile: \\:woman_technologist: \\:octocat: :unknown: :SMILE:"
let literal_doc = parse(literal, 'markdown')^
let written = format(literal_doc, 'markdown')^
count(literal_doc?symbol) == 0;
count(literal_doc?<img>) == 0;
count(parse(written, 'markdown')^?symbol) == 0;
count(parse(written, 'markdown')^?<img>) == 0;
format(parse(written, 'markdown')^, 'markdown')^ == written;
let split_literal = format(<doc <p ":" "smile:" " " ":octo" "cat:">>, 'markdown')^;
count(parse(split_literal, 'markdown')^?symbol) == 0;
count(parse(split_literal, 'markdown')^?<img>) == 0;

// Code, link destinations and HTML attributes stay literal; link labels expand.
let contexts = parse("`:smile:`\n\n```\n:octocat:\n```\n\n    :smile:\n\n[:smile:](https://example.com/:heart:) <span title=\":smile:\">:heart:</span>", 'markdown')^
count(contexts?symbol) == 2;
count(contexts?<img>) == 0;
(contexts?<a>).href == "https://example.com/:heart:";
count(parse(source, {type: "markup", flavor: "commonmark"})^?symbol) == 0;
count(parse(source, {type: "markup", flavor: "commonmark"})^?<img>) == 0;
contains(format(parse("👩‍💻 🇺🇸", 'markdown')^, 'html')^, "👩‍💻 🇺🇸");

// Editor import distinguishes real emoji from identical literal shortcode text.
let editor_sources = [
  "Faces :smile: :-1: :woman_technologist: :octocat: :shipit:.\n",
  "Literal \\:smile: and \\:octocat:.\n",
  "**:smile:** and [emoji :octocat:](https://example.com).\n",
  "`:smile:` and `:octocat:`.\n"
];
all([for (text in editor_sources) {
  let loaded = markdown.import_text(text)^
  let saved = markdown.export_text(loaded.doc, loaded.envelope)
  markdown.check_roundtrip(loaded.doc, loaded.envelope) == null and
    all([for (node in loaded.doc.content) node.tag != 'md_source']) and
    format(parse(text, 'markdown')^, 'markdown')^ == format(parse(saved, 'markdown')^, 'markdown')^
}])
