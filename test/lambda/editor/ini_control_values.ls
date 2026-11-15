import p: lambda.editor.format.key_value
let source = "[main]\nfirst=\fvalue\nsecond=\u000b\"quoted\"\nthird=ordinary\n"
let loaded = p.ini_descriptor.import_text(source)^
let first = loaded.doc.tree.children[0].node
let second = loaded.doc.tree.children[1].node
let changed = (p.replace_scalar_literal(loaded.doc, first.first, first.last, "\"\\fchanged\"") ^ { null })
let parsed = if (changed == null) null else parse(changed.doc.text, 'ini')^
let multiline = p.ini_descriptor.import_text("[s]\nkey=\"one\f\ntwo\"\nlast=x\n")^
let special = p.ini_descriptor.import_text("[s]\nkey=\u007fvalue\n")^;
[
  first.text == "\fvalue" and first.value_literal == true and first.display_text == "\"\\fvalue\"",
  second.text == "\u000b\"quoted\"" and second.value_literal == true,
  loaded.doc.tree.children[2].node.value_literal != true,
  changed != null and parsed.main.first == "\fchanged" and parsed.main.second == "\u000b\"quoted\"",
  (p.replace_scalar_literal(loaded.doc, first.first, first.last, "42") ^ { null }) == null,
  multiline.doc.tree.children[0].node.display_text == "\"\\\"one\\f\\ntwo\\\"\"" and len(multiline.doc.tree.children) == 2,
  special.doc.tree.children[0].node.display_text == "\"\\u007fvalue\""
]
