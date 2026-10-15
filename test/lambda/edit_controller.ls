import controller: lambda.edit.controller
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('paragraph',[text("Hello")])])
let start = text_selection(pos([0,0],0),pos([0,0],0))
let selected = text_selection(pos([0,0],4),pos([0,0],1))
let editor = {*:edit_open(doc,editor_schemas.markdown,start),native_selection_revision:4,model_revision:7}
let event = {source_selection:selected,source_pos:pos([0,0],2),selection_revision:5,
    selection_origin:"pointer",model_revision:7}
let toolbar = controller.selection_snapshot(event,false,false)
let control = controller.selection_snapshot(event,false,true)
let clicked = controller.selection_snapshot({source_pos:pos([0,0],3)},true,false)
let accepted = controller.accept_selection(editor,toolbar)
let stale = controller.accept_selection(accepted.editor,{*:toolbar,selection_revision:4})
let echo = controller.accept_selection(accepted.editor,{*:toolbar,source_selection:start,
    selection_origin:"model-commit",selection_revision:6})
let replacement = controller.replace({*:editor,mounted:true,preset:"markdown",surface_handle:"bound"},
    edit_open(node('doc',[node('paragraph',[text("New")])]),editor_schemas.markdown,start));
[
    toolbar.source_selection == selected, toolbar.source_pos == null,
    toolbar.selection_revision == 5, control == null,
    controller.selection_snapshot(event,false,true,true).source_selection == selected,
    controller.selection_snapshot(event,false,true,true).source_pos == null,
    controller.selection_snapshot(event,false,false,false) == null,
    controller.selection_snapshot({source_pos:pos([99],1)},false,false) == null,
    clicked.source_pos == pos([0,0],3), accepted.changed,
    accepted.editor.selection == selected, accepted.editor.native_selection_revision == 5,
    not stale.changed, stale.editor == accepted.editor,
    not echo.changed, echo.editor.selection == selected, echo.editor.native_selection_revision == 6,
    controller.accept_selection(editor,null).editor == editor,
    replacement.mounted, replacement.surface_handle == "bound", replacement.preset == "markdown",
    replacement.model_revision == 8, replacement.native_selection_revision == 4,
    doc_text(replacement.doc) == "New", len(replacement.history.undo) == 0,
    editor.selection == start, editor.model_revision == 7
]
