// lambda.edit SVG adapter and drawing commands
// (vibe/radiant/Radiant_Design_Edit_Mode.md §6): the source tree is the model,
// the prologue is kept as text, edits are Steps on the tree, and the surface
// projection adds only editor-owned markers.
import sv: lambda.edit.svg
import dr: lambda.edit.drawing
import lambda.edit.model
import lambda.editor.mod_doc

let source = "<?xml version=\"1.0\"?>
<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" [
  <!ENTITY ns \"http://www.w3.org/2000/svg\">
]>
<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"100\">
  <defs><clipPath id=\"clip\"><rect width=\"50\" height=\"50\"/></clipPath></defs>
  <g id=\"shape\" clip-path=\"url(#clip)\" onclick=\"alert(1)\"><rect id=\"inner\" x=\"1\" y=\"2\" width=\"3\" height=\"4\"/></g>
  <rect id=\"plain\" x=\"10\" y=\"10\" width=\"20\" height=\"10\" style=\"fill:red;opacity:0.5\"/>
  <line id=\"ln\" x1=\"0\" y1=\"0\" x2=\"10\" y2=\"20\"/>
  <text id=\"t\" x=\"5\" y=\"90\">Hi <tspan>there</tspan></text>
  <script>alert(2)</script>
</svg>
<!-- trailing -->
"

let loaded = sv.import_text(source)
let doc = loaded.doc
fn attrs_s(n) => join([for (a in n.attrs) string(a.name) ++ "=" ++ string(a.value)], " ")
fn tags(d) => [for (c in d.content) c.tag]

"prologue and epilogue:"; [loaded.envelope.prologue, loaded.envelope.epilogue];
"top-level:"; tags(doc);
"written back unchanged in meaning:"; [sv.check_roundtrip(doc, loaded.envelope) == null];
"view box without a viewBox:"; dr.view_box(doc);

let moved = dr.move_tx(doc, [1, 2, 3, 4], 5.0, 5.0).doc_after
"move by coordinates or translate:"; [for (i in 1 to 4) attrs_s(moved.content[i])];
let again = dr.move_tx(moved, [1], 1.5, -1.0).doc_after
"a second move merges the translate:"; [attrs_s(again.content[1])];

"resizable:"; [for (i in 1 to 4) dr.resizable(doc.content[i])];
let wide = dr.resize_tx(doc, 3, dr.resized_box(dr.box(0.0, 0.0, 10.0, 20.0), "e", 10.0, 0.0)).doc_after
"a line resized by its box:"; [attrs_s(wide.content[3])];
"a handle past the opposite edge flips the box:"; dr.resized_box(dr.box(0.0, 0.0, 10.0, 10.0), "nw", 15.0, 15.0);

let copies = dr.duplicate_tx(doc, [1])
"duplicate:"; [copies.picked, attrs_s(copies.tx.doc_after.content[2]), attrs_s(copies.tx.doc_after.content[2].content[0])];

let painted = dr.paint_tx(doc, [2, 3], [["fill", "blue"], ["stroke", "black"], ["stroke-width", ""]]).doc_after
"paint in style or attribute:"; [attrs_s(painted.content[2]), attrs_s(painted.content[3])];
"paint values:"; [dr.paint_value(doc.content[2], "fill"), dr.paint_value(doc.content[2], "stroke")];

let front = dr.order_tx(doc, 1, true)
"to front:"; [front.index, tags(front.tx.doc_after)];
let back = dr.order_tx(doc, 4, false)
"to back keeps defs first:"; [back.index, tags(back.tx.doc_after)];
"already at the back:"; [dr.order_tx(doc, 1, false) == null];

"text editable:"; [dr.text_editable(doc.content[4]),
                   dr.text_editable(dr.new_text(doc, {x: 1.0, y: 2.0}, "note", dr.new_state().style))];
let shape = dr.new_shape(doc, 'ellipse', {x: 10.0, y: 10.0}, {x: 10.5, y: 10.5}, dr.new_state().style)
"a click places a default shape:"; [attrs_s(shape)];

let ds = {*: dr.new_state(), picked: [{index: 2, box: dr.box(10.0, 10.0, 20.0, 10.0)}]}
let projection = dr.projection_xml(doc, ds)
"projection:"; [contains(projection, "data-edit-path=\"2\""), contains(projection, "onclick"),
                contains(projection, "<script"), contains(projection, "edit-handle"),
                contains(projection, "width=\"200\" height=\"100\" viewBox=\"0 0 200 100\"")];
"saved text never holds the editor's marks:";
[contains(sv.export_text(copies.tx.doc_after, loaded.envelope), "data-edit")]
