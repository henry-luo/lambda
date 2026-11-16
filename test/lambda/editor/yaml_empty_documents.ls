import y: lambda.editor.format.yaml
let text = "---\n---\na: 1\n---\n"
let loaded = y.import_text(text)^
let first = loaded.doc.tree.children[0].node
let last_doc = loaded.doc.tree.children[2].node
let filled_first = y.replace_scalar(loaded.doc, first.first, first.last, "hello")^
let filled_last = y.replace_scalar(loaded.doc, last_doc.first, last_doc.last, "world")^
let single = y.import_text("---")^
let single_changed = y.replace_scalar(single.doc, single.doc.tree.first, single.doc.tree.last, "value")^;
[
  len(loaded.doc.tree.children) == 3,
  first.empty_doc == true and last_doc.empty_doc == true,
  filled_first.doc.text == "---\nhello\n---\na: 1\n---\n",
  filled_last.doc.text == "---\n---\na: 1\n---\nworld\n",
  single_changed.doc.text == "---\nvalue\n",
  (y.replace_scalar(loaded.doc, first.first, first.last, "hello\nextra: 1") ^ { null }) == null
]
