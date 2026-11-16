import y: lambda.editor.format.yaml
let text = "base: &answer 42 # retained\nother: &other 2\ncopy: *answer\ntagged: !!str hello\n"
let loaded = y.import_text(text)^
let root = loaded.doc.tree
let alias = root.children[2].node
let tag = root.children[3].node
let anchor = root.children[0].node
let changed = y.replace_scalar(loaded.doc, alias.first, alias.last, "*other")^
let retagged = y.replace_scalar(loaded.doc, tag.first, tag.last, "!!str world")^;
[
  len(root.children) == 4,
  anchor.text == "&answer 42" and alias.text == "*answer" and tag.text == "!!str hello",
  changed.doc.text == "base: &answer 42 # retained\nother: &other 2\ncopy: *other\ntagged: !!str hello\n",
  contains(retagged.doc.text, "tagged: !!str world\n"),
  (y.replace_scalar(loaded.doc, alias.first, alias.last, "*missing") ^ { null }) == null,
  (y.replace_scalar(loaded.doc, anchor.first, anchor.last, "&changed 42") ^ { null }) == null,
  (y.replace_scalar(loaded.doc, alias.first, alias.last, "*answer\ninjected: 1") ^ { null }) == null
]
