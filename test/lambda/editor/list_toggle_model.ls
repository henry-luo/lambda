import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.source_pos
import lambda.editor.core.commands
import lambda.editor.schema.markdown

fn item(s) => node('li', [node('p', [text(s)])])
let source = node('doc', [node_attrs('ol', [{name:'start',value:"4"}], [item("one"),item("two"),item("three")])])
let selected = {doc:source, schema:html5_subset_schema,
  selection:text_selection(pos([0,1,0,0],1),pos([0,1,0,0],1))}
let lifted = cmd_wrap_list(selected, 'ordered')^
let switched = cmd_wrap_list(selected, 'bullet')^
let all = {doc:source, schema:html5_subset_schema, selection:node_selection([0])}
let unwrapped = cmd_wrap_list(all, 'ordered')^
let protected = node('doc', [node('ul', [node_attrs('li', [{name:'id',value:'keep'}], [text("item")])])])
let rejected = cmd_wrap_list({doc:protected, schema:html5_subset_schema, selection:node_selection([0])}, 'bullet')^;
let tagged = node('doc', [node_attrs('ul', [{name:'id',value:'list-id'}], [item("item")])])
let tagged_result = cmd_wrap_list({doc:tagged, schema:html5_subset_schema, selection:node_selection([0])}, 'bullet')^
let nested = node('doc', [node('ul', [node('li', [text("before"), node('ul', [node('li', [text("child")])]), text("after")])])])
let nested_result = cmd_wrap_list({doc:nested, schema:html5_subset_schema, selection:node_selection([0])}, 'bullet')^
let selected_range = {doc:source, schema:html5_subset_schema,
  selection:text_selection(pos([0,0,0,0],0),pos([0,1,0,0],3))}
let lifted_range = cmd_wrap_list(selected_range, 'ordered')^
let nested_selection = {doc:nested, schema:html5_subset_schema,
  selection:text_selection(pos([0,0,2],3),pos([0,0,0],2))}
let nested_selected_result = cmd_wrap_list(nested_selection, 'bullet')^
let plain_doc = node('doc', [node('p',[text("first")]),node('p',[text("second")])])
let wrapped = cmd_wrap_list({doc:plain_doc, schema:html5_subset_schema,
  selection:text_selection(pos([1,0],3),pos([0,0],2))}, 'bullet')^
let boundary_state = {doc:source, schema:html5_subset_schema,
  selection:text_selection(pos([0,0,0,0],1),pos([0,1,0,0],0))}
let boundary_lift = cmd_wrap_list(boundary_state, 'ordered')^
let boundary_convert = cmd_wrap_list(boundary_state, 'bullet')^
let reverse_boundary = cmd_wrap_list({*:boundary_state,
  selection:text_selection(boundary_state.selection.head,boundary_state.selection.anchor)}, 'ordered')^
let split_doc = node('doc', [node('ol', [item("one"),node('li', [node('p', [text("two"),text(" more")])]),item("three")])])
let within_later_item = cmd_wrap_list({doc:split_doc, schema:html5_subset_schema,
  selection:text_selection(pos([0,0,0,0],1),pos([0,1,0,1],0))}, 'ordered')^
let explicit_doc = node('doc', [node_attrs('ol', [{name:'start',value:"4"}], [
  node_attrs('li', [{name:'value',value:"10"}], [node('p',[text("one")])]), item("two"), item("three")])])
let explicit_lift = cmd_wrap_list({doc:explicit_doc, schema:html5_subset_schema,
  selection:text_selection(pos([0,1,0,0],1),pos([0,1,0,0],1))}, 'ordered')^
let zero_doc = node('doc', [node_attrs('ol', [{name:'start',value:"-1"}], [item("one"),item("two"),item("three")])])
let zero_lift = cmd_wrap_list({doc:zero_doc, schema:html5_subset_schema,
  selection:text_selection(pos([0,1,0,0],1),pos([0,1,0,0],1))}, 'ordered')^
let styled = node('doc', [node_attrs('ol', [{name:'style',value:"list-style-type: upper-roman; color: red"}], [item("one")])])
let styled_switch = cmd_wrap_list({doc:styled, schema:html5_subset_schema, selection:node_selection([0])}, 'bullet')^
let styled_unwrap = cmd_wrap_list({doc:styled, schema:html5_subset_schema, selection:node_selection([0])}, 'ordered')^
let marker_only = node('doc', [node_attrs('ol', [{name:'style',value:"list-style-type: upper-roman"}], [item("one")])])
let marker_unwrap = cmd_wrap_list({doc:marker_only, schema:html5_subset_schema, selection:node_selection([0])}, 'ordered')^;
[
  lifted.doc_after.content[0].tag == 'ol' and attrs_get(lifted.doc_after.content[0].attrs,'start') == "4",
  lifted.doc_after.content[1].tag == 'p' and doc_text(lifted.doc_after.content[1]) == "two",
  lifted.doc_after.content[2].tag == 'ol' and attrs_get(lifted.doc_after.content[2].attrs,'start') == "6",
  switched.doc_after.content[1].tag == 'ul' and doc_text(switched.doc_after.content[1]) == "two",
  attrs_get(switched.doc_after.content[2].attrs,'start') == "6",
  [for (block in unwrapped.doc_after.content) doc_text(block)] == ["one","two","three"],
  rejected == null, tagged_result == null,
  [for (block in nested_result.doc_after.content) block.tag] == ['p','ul','p'],
  [for (block in nested_result.doc_after.content) doc_text(block)] == ["before","child","after"],
  [for (block in lifted_range.doc_after.content) block.tag] == ['p','p','ol'],
  attrs_get(lifted_range.doc_after.content[2].attrs, 'start') == "6",
  lifted.sel_after.anchor.path == [1,0] and lifted.sel_after.anchor.offset == 1,
  switched.sel_after.anchor.path == [1,0,0,0] and switched.sel_after.anchor.offset == 1,
  lifted_range.sel_after.anchor.path == [0,0] and lifted_range.sel_after.head.path == [1,0] and
    lifted_range.sel_after.anchor.offset == 0 and lifted_range.sel_after.head.offset == 3,
  nested_selected_result.sel_after.anchor.path == [2,0] and nested_selected_result.sel_after.anchor.offset == 3,
  nested_selected_result.sel_after.head.path == [0,0] and nested_selected_result.sel_after.head.offset == 2,
  wrapped.sel_after.anchor.path == [0,1,0,0] and wrapped.sel_after.anchor.offset == 3,
  wrapped.sel_after.head.path == [0,0,0,0] and wrapped.sel_after.head.offset == 2,
  [for (block in boundary_lift.doc_after.content) block.tag] == ['p','ol'],
  attrs_get(boundary_lift.doc_after.content[1].attrs,'start') == "5" and
    len(boundary_lift.doc_after.content[1].content) == 2,
  boundary_lift.sel_after.anchor.path == [0,0] and boundary_lift.sel_after.anchor.offset == 1 and
    boundary_lift.sel_after.head.path == [1,0,0,0] and boundary_lift.sel_after.head.offset == 0,
  [for (block in boundary_convert.doc_after.content) block.tag] == ['ul','ol'] and
    len(boundary_convert.doc_after.content[1].content) == 2,
  reverse_boundary.sel_after.anchor.path == [1,0,0,0] and reverse_boundary.sel_after.head.path == [0,0],
  [for (block in within_later_item.doc_after.content) block.tag] == ['p','p','ol'],
  attrs_get(explicit_lift.doc_after.content[2].attrs,'start') == "12",
  attrs_get(explicit_lift.doc_after.content[0].content[0].attrs,'value') == "10",
  attrs_get(zero_lift.doc_after.content[2].attrs,'start') == null,
  styled_switch.doc_after.content[0].tag == 'ul' and attrs_get(styled_switch.doc_after.content[0].attrs,'style') == "color: red",
  styled_unwrap == null,
  marker_unwrap.doc_after.content[0].tag == 'p'
]
