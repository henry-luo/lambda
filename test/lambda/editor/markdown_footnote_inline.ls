import md: lambda.editor.format.markdown
import lambda.editor.core.doc
import lambda.editor.format.model

let source = "Before [^one] and **bold [^two] after**.\n\n[^one]: First note.\n\n[^two]: Second note.\n"
let loaded = md.import_text(source)^
let paragraph = loaded.doc.content[0]
let changed = replace_node_at(loaded.doc, [0, 0], text("Edited "))^
let output = md.export_text(changed, loaded.envelope)^
let reopened = md.import_text(output)^
let variants = [
  "# Heading [^n]\n\n[^n]: Note.\n",
  "- List [^n]\n- More text\n\n[^n]: Note.\n",
  "| Key | Value |\n| --- | --- |\n| item | cell [^n] |\n\n[^n]: Note.\n",
  "Before [^n] and [^n] after.\n\n[^n]: Note.\n",
  "Before [^MiXeD] after.\n\n[^mixed]: Note.\n",
  "[^n] at start.\n\n[^n]: Note.\n",
  "At end [^n]\n\n[^n]: Note.\n"]
let checks = [for (variant in variants) {
  let document = md.import_text(variant)^;
  document.doc.content[0].tag != 'md_source' and md.check_roundtrip(document.doc, document.envelope)^ == null
}];
[
  paragraph.tag == 'p',
  len([for (item in paragraph.content where item.tag == 'footnote_ref') item]) == 2,
  attr_get(paragraph.content[1], 'label') == "one",
  contains(output, "Edited [^one] and **bold [^two] after**."),
  contains(output, "[^one]: First note.\n\n[^two]: Second note."),
  md.check_roundtrip(changed, loaded.envelope)^ == null,
  md.normal_form(reopened.doc) == md.normal_form(changed),
  checks == [true, true, true, true, true, true, true]
]
