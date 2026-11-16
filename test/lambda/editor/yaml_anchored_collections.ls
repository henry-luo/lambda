import y: lambda.editor.format.yaml
let text = "base: &root\n  child: old\ncopy: *root\nflow: !thing &flow [1, 2]\nobject: &o {a: 1}\nempty: &e []\n"
let tagged = y.import_text("value: !<tag:example.com,2026:thing> {key: 1}\nquoted: [!!str \"a,b\", 2]\nblock: &block |\n  hello\n  world\n")^
let tagged_value = tagged.doc.tree.children[0].node.children[0].node
let tag_edit = y.replace_scalar(tagged.doc, tagged_value.first, tagged_value.last, "4")^
let quoted = tagged.doc.tree.children[1].node.children[0].node
let block = tagged.doc.tree.children[2].node
let loaded = y.import_text(text)^
let root = loaded.doc.tree
let child = root.children[0].node.children[0].node
let flow = root.children[2].node
let obj = root.children[3].node
let empty = root.children[4].node
let changed = y.replace_scalar(loaded.doc, child.first, child.last, "new")^
let added = y.insert_child(loaded.doc, empty.first, "", "3")^
let changed_flow = y.replace_scalar(loaded.doc, flow.children[1].node.first, flow.children[1].node.last, "4")^;
let directive_text = "%TAG !e! tag:example.org,2026:\n---\na: !e!thing one\nb: !e!thing [1, 2]\n"
let directive = y.import_text(directive_text)^
let directive_child = directive.doc.tree.children[1].node.children[0].node
let directive_edit = y.replace_scalar(directive.doc, directive_child.first, directive_child.last, "3")^;
[
  len(root.children) == 5,
  root.children[0].node.kind == 'object' and flow.kind == 'array' and obj.kind == 'object',
  root.children[1].node.text == "*root",
  contains(changed.doc.text, "base: &root\n  child: new\ncopy: *root"),
  contains(added.doc.text, "empty: &e [3]\n"),
  contains(changed_flow.doc.text, "flow: !thing &flow [1, 4]\n"),
  parse(changed.doc.text,'yaml')^.copy.child == "new",
  contains(tag_edit.doc.text, "!<tag:example.com,2026:thing> {key: 4}"),
  quoted.text == "!!str \"a,b\"",
  block.text == "&block |\n  hello\n  world",
  directive_edit.doc.text == "%TAG !e! tag:example.org,2026:\n---\na: !e!thing one\nb: !e!thing [3, 2]\n"
]
