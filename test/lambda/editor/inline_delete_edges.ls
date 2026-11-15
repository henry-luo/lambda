import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.source_pos
import lambda.editor.core.commands
import lambda.editor.schema.markdown

fn editor_state(doc, path, offset) => {doc:doc, schema:html5_subset_schema, selection:text_selection(pos(path,offset),pos(path,offset))}
let page = node_attrs('br', [{name:'rtf-page', value:true}], [])
let doc = node('doc', [node('paragraph', [text("one"), page, text("two")])])
let back = cmd_delete_backward(editor_state(doc,[0,2],0))
let forward = cmd_delete_forward(editor_state(doc,[0,0],3))
let mixed = node('doc', [node('paragraph', [text_marked("ab", [mark('strong',true)]), text("cd")])])
let cross_mark = cmd_delete_backward(editor_state(mixed,[0,1],0))
let distinct = node('doc', [node('paragraph', [text_marked("a",[mark('strong',true)]), page, text_marked("b",[mark('em',true)])])])
let distinct_back = cmd_delete_backward(editor_state(distinct,[0,2],0))
let atom_range = text_selection(pos([0,1],0),pos([0,2],0))
let target_back = cmd_delete_backward({doc:doc,schema:html5_subset_schema,selection:atom_range})
let forward_range = text_selection(pos([0,0],3),pos([0,1],0))
let target_forward = cmd_delete_forward({doc:doc,schema:html5_subset_schema,selection:forward_range});
[
  back != null and doc_text(back.doc_after) == "onetwo" and len(back.doc_after.content[0].content) == 1,
  back != null and back.sel_after.anchor.path == [0,0] and back.sel_after.anchor.offset == 3,
  forward != null and doc_text(forward.doc_after) == "onetwo" and forward.sel_after.anchor.offset == 3,
  cross_mark != null and doc_text(cross_mark.doc_after) == "acd" and has_mark(cross_mark.doc_after.content[0].content[0].marks,'strong'),
  distinct_back != null and len(distinct_back.doc_after.content[0].content) == 2,
  distinct_back != null and distinct_back.sel_after.anchor.path == [0,1] and distinct_back.sel_after.anchor.offset == 0,
  target_back != null and doc_text(target_back.doc_after) == "onetwo" and target_back.sel_after.anchor.offset == 3,
  target_forward != null and doc_text(target_forward.doc_after) == "onetwo" and target_forward.sel_after.anchor.offset == 3
]
