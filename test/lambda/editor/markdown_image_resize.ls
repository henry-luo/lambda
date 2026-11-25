import md: lambda.editor.format.markdown
import image: lambda.editor.image
import lambda.editor.model
import lambda.editor.core.doc
import lambda.editor.core.step

let source = "Before ![A & B](photo.png) after.\n\n<p align=\"center\">\n  <img src=\"one.png\" width=\"49%\" />\n  <img src=\"two.png\" width=\"49%\" />\n</p>\n<p>See <a href=\"gallery.html\">gallery</a>.</p>\n\n<div class=\"keep\">Other HTML</div>\n"
let loaded = md.import_text(source)^
let start = edit_open(loaded.doc, md.schema, null)
let paths = image.images(start.doc)
let first = image.resize_image(start, paths[0].path, 240, 150)^
let second = image.resize_image(first, paths[1].path, 120, 75)^
let written = md.export_text(second.doc, loaded.envelope)^
let restored = md.import_text(written)^
let images = image.images(restored.doc);
[
  len(paths) == 3,
  md.export_text(start.doc, loaded.envelope)^ == source,
  md.check_roundtrip(second.doc, loaded.envelope)^ == null,
  attrs_get(images[0].node.attrs, 'width') == 240,
  attrs_get(images[0].node.attrs, 'alt') == "A & B",
  string(attrs_get(images[1].node.attrs, 'width')) == "120",
  attrs_get(images[2].node.attrs, 'width') == "49%",
  contains(written, "<p align=\"center\">"),
  contains(written, "<a href=\"gallery.html\">gallery</a>"),
  contains(written, "<div class=\"keep\">Other HTML</div>"),
  edit_exec(second, edit_cmd_history_undo()).doc == first.doc
]
