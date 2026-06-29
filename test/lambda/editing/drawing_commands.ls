// Stage-4 drawing commands (mod_drawing_commands). Every command lowers to the
// existing set_attr / replace steps (no new step kinds).
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_geom
import lambda.package.editor.mod_drawing_commands

fn apply_all(steps, doc, i, n) { if (i >= n) { doc } else { apply_all(steps, step_apply(steps[i], doc), i + 1, n) } }

// doc: [paragraph, drawing[layer[S1, S2]]]
fn mk_doc() {
  let s1 = make_rect_shape("S1", {x:50.0,y:50.0,width:100.0,height:80.0}, {})
  let s2 = make_rect_shape("S2", {x:200.0,y:100.0,width:120.0,height:80.0}, {fill:"#eef"})
  let layer = node_attrs('layer', [{name:'id',value:"L1"}], [s1, s2])
  node_attrs('doc', [], [node_attrs('paragraph',[],[text("Before")]),
                         node_attrs('drawing',[{name:'id',value:"D1"}],[layer])])
}
let st = {doc: mk_doc(), selection: null}

// 1. constructors lower to plain-map shape nodes
let r0 = make_rect_shape("R0", {x:1.0,y:2.0,width:3.0,height:4.0}, {fill:"#abc"})
"rect_kind:"; get_shape_kind(r0)
"rect_fill:"; get_shape_attr_string(r0, 'fill', "") == "#abc"
"rect_default_stroke:"; get_shape_attr_string(r0, 'stroke', "") == "#000"
"ellipse_kind:"; get_shape_kind(make_ellipse_shape("E0", {x:0.0,y:0.0,width:5.0,height:5.0}, {}))

// 2. insert
let txi = cmd_insert_shape(st, {drawing_path:[1], layer_index:0,
  shape: make_ellipse_shape("S3", {x:0.0,y:0.0,width:10.0,height:10.0}, {})})
"insert_steps:"; len(txi.steps)
"insert_count:"; len(node_at(txi.doc_after, [1,0]).content)
"insert_new_id:"; get_shape_attr_string(node_at(txi.doc_after, [1,0,2]), 'id', "") == "S3"
"insert_bad_drawing:"; cmd_insert_shape(st, {drawing_path:[0], layer_index:0, shape:r0}) == null

// 3. move (one transaction, set_attr x + y); inverse restores original
let txm = cmd_move_shapes(st, {shape_paths:[[1,0,0]], dx:80.0, dy:50.0})
"move_steps:"; len(txm.steps)
"move_x:"; get_shape_attr_number(node_at(txm.doc_after,[1,0,0]),'x',0.0)
"move_y:"; get_shape_attr_number(node_at(txm.doc_after,[1,0,0]),'y',0.0)
"move_noop:"; cmd_move_shapes(st, {shape_paths:[[1,0,0]], dx:0.0, dy:0.0}) == null
let inv = tx_invert(txm)
"move_invert_x:"; get_shape_attr_number(node_at(apply_all(inv.steps, txm.doc_after, 0, len(inv.steps)),[1,0,0]),'x',0.0)

// move multiple shapes in one transaction
let txm2 = cmd_move_shapes(st, {shape_paths:[[1,0,0],[1,0,1]], dx:10.0, dy:0.0})
"move_multi_steps:"; len(txm2.steps)
"move_multi_s2x:"; get_shape_attr_number(node_at(txm2.doc_after,[1,0,1]),'x',0.0)

// 4. resize (only changed dims emit steps)
let txr = cmd_resize_shape(st, {shape_path:[1,0,1], width:200.0})
"resize_steps:"; len(txr.steps)
"resize_w:"; get_shape_attr_number(node_at(txr.doc_after,[1,0,1]),'width',0.0)
"resize_noop:"; cmd_resize_shape(st, {shape_path:[1,0,1], width:120.0}) == null

// 5. rotate
let txrot = cmd_rotate_shape(st, [1,0,0], 45.0)
"rotate_val:"; get_shape_attr_number(node_at(txrot.doc_after,[1,0,0]),'rotate',0.0)
"rotate_noop:"; cmd_rotate_shape(st, [1,0,0], 0.0) == null

// 6. set arbitrary attr
let txa = cmd_set_shape_attr(st, [1,0,0], 'fill', "#f00")
"setattr_fill:"; get_shape_attr_string(node_at(txa.doc_after,[1,0,0]),'fill',"") == "#f00"

// 7. delete (descending order so indices stay valid)
let txd = cmd_delete_shapes(st, [[1,0,0],[1,0,1]])
"delete_steps:"; len(txd.steps)
"delete_remaining:"; len(node_at(txd.doc_after,[1,0]).content)
let txd1 = cmd_delete_shapes(st, [[1,0,0]])
"delete_one_remaining:"; len(node_at(txd1.doc_after,[1,0]).content)
"delete_kept_id:"; get_shape_attr_string(node_at(txd1.doc_after,[1,0,0]),'id',"") == "S2"

// 8. z-order
let txf = cmd_bring_to_front(st, [1,0,0])
"front_steps:"; len(txf.steps)
"front_last_id:"; get_shape_attr_string(node_at(txf.doc_after,[1,0,1]),'id',"") == "S1"
"front_noop:"; cmd_bring_to_front(st, [1,0,1]) == null
let txb = cmd_send_to_back(st, [1,0,1])
"back_first_id:"; get_shape_attr_string(node_at(txb.doc_after,[1,0,0]),'id',"") == "S2"
"back_noop:"; cmd_send_to_back(st, [1,0,0]) == null

// 9. connector constructor
let c = make_connector("C1", {from_shape:"S1", to_shape:"S2"})
"conn_tag:"; c.tag
"conn_from:"; get_shape_attr_string(c, 'from-shape', "") == "S1"
"conn_to:"; get_shape_attr_string(c, 'to-shape', "") == "S2"
"conn_routing:"; attrs_get(c.attrs, 'routing')
let cf = make_connector("C2", {from_x:0.0, from_y:0.0, to_x:10.0, to_y:10.0, routing:'straight'})
"conn_free_from_x:"; get_shape_attr_number(cf, 'from-x', -1.0)
"conn_free_routing:"; attrs_get(cf.attrs, 'routing')
