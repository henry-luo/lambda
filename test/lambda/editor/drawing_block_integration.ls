// Stage-4 schema seam: the editor recognises an inline <drawing> block via the
// combined doc_schema (registered as editor_schemas.doc). Verifies the drawing
// is an atomic, editable embed (flow-mode descends into it; its shapes are the
// selectable units) and that a node-selection on the drawing deletes it as a
// whole block.
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_dom_bridge
import lambda.package.editor.mod_doc_schema
import lambda.package.editor.mod_editor

// 1. registry wiring: editor_schemas.doc is the drawing-capable schema
"registry_has_doc:"; editor_schemas.doc.drawing != null
"registry_drawing_atomic:"; editor_schemas.doc.drawing.atomic
"registry_drawing_editable:"; editor_schemas.doc.drawing.editable
"registry_shape_role:"; editor_schemas.doc.shape.role
"registry_is_doc_schema:"; editor_schemas.doc.layer.role == doc_schema.layer.role

// a flow doc with an inline drawing (plain-map editor representation)
let ddoc = node('doc', [
  node('paragraph', [text("A")]),
  node_attrs('drawing', [{name: 'id', value: "D1"}], [
    node_attrs('layer', [{name: 'id', value: "L1"}], [
      node_attrs('shape', [{name: 'id', value: "S1"}, {name: 'kind', value: 'rect'},
        {name: 'x', value: 0.0}, {name: 'y', value: 0.0},
        {name: 'width', value: 10.0}, {name: 'height', value: 10.0}], [])
    ])
  ]),
  node('paragraph', [text("B")])
])

// 2. dom-bridge: a click inside the drawing resolves to the nearest SELECTABLE
//    ancestor. A shape is selectable -> selects the shape; the drawing block
//    itself is editable -> descend (no atomic node-selection -> null).
"shape_is_selectable:"; path_equal(nearest_selectable_path(doc_schema, ddoc, [1, 0, 0]), [1, 0, 0])
"drawing_descends:"; nearest_selectable_path(doc_schema, ddoc, [1]) == null
"layer_descends:"; nearest_selectable_path(doc_schema, ddoc, [1, 0]) == null

// 3. node-selection on the <drawing> block is a valid whole-block selection
let r = resolve_pos(ddoc, pos([1], 0))
"drawing_node_resolves:"; r.found
"drawing_node_tag:"; r.node.tag

// 4. open an editor on the drawing doc via the registered schema and delete the
//    drawing as one unit (node selection -> delete removes the whole block)
let ed = edit_open(ddoc, editor_schemas.doc, node_selection([1]))
"editor_doc_count:"; len(ed.doc.content)
let ed2 = edit_exec(ed, edit_cmd_delete_backward())
"after_delete_count:"; len(ed2.doc.content)
"after_delete_text:"; doc_text(ed2.doc) == "AB"
"after_delete_first:"; ed2.doc.content[0].tag
