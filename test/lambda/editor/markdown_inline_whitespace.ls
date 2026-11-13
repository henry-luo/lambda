import md: lambda.editor.format.markdown

let spaced = md.import_text("one [link](https://example.org) two\n")^
let joined = md.import_text("one[link](https://example.org)two\n")^
let inner = md.import_text("one [ link ](https://example.org) two\n")^;
[
  md.normal_form(spaced.doc) != md.normal_form(joined.doc),
  md.normal_form(spaced.doc) != md.normal_form(inner.doc),
  md.check_roundtrip(spaced.doc, spaced.envelope)^ == null,
  md.check_roundtrip(inner.doc, inner.envelope)^ == null
]
