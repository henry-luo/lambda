import controller: lambda.edit.controller
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

pn main() {
    let editor = edit_open(node('doc',[node('paragraph',[text("Hello")])]),editor_schemas.markdown,
        text_selection(pos([0,0],5),pos([0,0],5)))
    let copied = controller.handle(editor,{input_type:"copy",target:null})
    let blocked = controller.handle(editor,{input_type:"insertText",data:"!",target:null},true)
    let absent = controller.mount(editor,null)
    let disabled = controller.mount(editor,"unbound",null)
    let restored = controller.restore(editor,false)
    let inserted = controller.request(editor,"insertText",{data:"!"})
    let undone = controller.request(inserted.editor,"historyUndo")
    return [copied.editor == editor,copied.result.claimed,not copied.result.changed,
        not copied.result.history_recorded,copied.result.selection_after == editor.selection,
        blocked.editor == editor,not blocked.result.claimed,blocked.result.failure == "read-only",
        absent == editor,disabled == editor,not restored,doc_text(inserted.editor.doc) == "Hello!",
        inserted.result.history_recorded,len(inserted.editor.history.undo) == 1,
        doc_text(undone.editor.doc) == "Hello",undone.editor.selection == editor.selection,
        undone.editor.model_revision == 2]
}
