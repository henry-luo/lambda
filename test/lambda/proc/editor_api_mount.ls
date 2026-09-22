// edit_mount is a procedure: given a DOM node it binds the model to a native
// edit surface, an effect a function may not have (S12.1.1v2). A string
// surface binds nothing, but the call still needs a pn context.
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

pn main() {
    let d0 = node('doc', [node('paragraph', [text("Hello")])])
    let caret = text_selection(pos([0, 0], 5), pos([0, 0], 5))
    let editor0 = edit_open(d0, editor_schemas.markdown, caret)
    let mounted = edit_mount(editor0, 'window0', 'markdown_wysiwyg')
    print("mount flag: "); print(mounted.mounted); print("\n")
    print("mount event: "); print(mounted.events[0].kind == 'mount'); print("\n")
    print("mount keeps editor pure: "); print(editor0.mounted); print("\n")
}
