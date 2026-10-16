// A native three-pane Notes workspace, using the Finder/Word demo conventions.
import dom
import lambda.editor.mod_editor
import controller: lambda.edit.controller
import resources: ~~.mod_resources
import app: .mod_app
import model: .mod_model
import ui: .mod_ui
import images: .mod_images
import result: lambda.dom.edit_result
import lambda.editor.mod_step

let CSS = input(url_resolve(resources.entry_uri()^,"note.css"),'text')^

edit <notes_app> state st:app.initial() {
    <body id:"notes",class:if (st.sidebar) "" else "sidebar-hidden",
        ["data-selected"]:st.selected,["data-folder"]:st.folder,["data-dirty"]:string(model.dirty(st)),
        ui.toolbar(st);
        <main class:"workspace",ui.sidebar(st);ui.note_list(st);ui.editor(st)>
        ui.menus(st);ui.dialog(st)
    >
}
on beforeinput() { return 'pass' }
on editaction(evt) {
    let image = model.selected_image(st.editor)
    let run = controller.handle(st.editor,evt,st.dialog!=null or st.selected=="" or model.current(st).deleted,
        "html5_subset","edit-surface",if (image==null) null else st.editor.selection)
    if (run.result.failure is null and evt.input_type!="copy") {
        st = model.update_editor(st,run.editor,app.stamp())
        app.finish(st,evt.target)
    }
    return if (image!=null and (evt.input_type=="copy" or evt.input_type=="deleteByCut"))
        result.with_clipboard_image(run.result,attrs_get(image.node.attrs,'src'),model.image_html(image),attrs_get(image.node.attrs,'alt') or "")
        else run.result
}
on selectionchange(evt) {
    let adopted = app.adopt(st,evt)
    if (adopted.editor!=st.editor) {
        st = adopted
        // adopting the range redraws its DOM nodes; complete against the replacement surface.
        controller.restore(st.editor)
    }
}
on note_command(req) {
    let previous = st.selected
    st = app.adopt(st,req.event)
    if (st.menu=="context") { st = {*:st,menu:""} }
    if (starts_with(req.id,"clipboard.")) {
        app.finish(st,req.node,true)
        controller.focus(req.node)
        dom.keyboard_command(controller.surface(req.node),model.clipboard_command(req.id))
        return 'handled'
    }
    st = app.command(st,req.id,req.params,req.node)
    // toolbar view changes redraw the same note and need its selection completion too.
    app.finish(st,req.node,st.selected==previous or req.id=="note.new")
    if (req.id=="note.select" or req.id=="folder.select")
        dom.focus_set(dom.get_element_by_id(dom.root_node(req.node),"note-list"),false)
}
on control_change(req) {
    st = app.control_change(st,req)
    app.finish(st,req.node)
}
on mousedown(evt) {
    if (evt.button==1 and dom.closest(evt.target,"#edit-surface")!=null) { return 'prevent-default' }
    let image = if (evt.button==0) images.hit(st.editor,evt.target) else null
    if (image!=null and st.dialog==null and not model.current(st).deleted) {
        st = {*:st,editor:model.select_image(controller.mount(st.editor,evt.target),image),menu:""}
        controller.focus(evt.target)
        app.finish(st,evt.target,true)
        return 'prevent-default'
    }
    // checkbox presses must cancel native caret placement before the generated check glyph is hit.
    return if (evt.button==0 and ui.task_hit(st,evt)!=null) 'prevent-default' else 'pass'
}
on click(evt) {
    if (evt.button!=null and evt.button!=0) { return 'pass' }
    let path = ui.task_hit(st,evt)
    if (path==null) {
        if (st.menu!="" and dom.closest(evt.target,".popover,.editor-tools")==null) {
            st = {*:app.adopt(st,evt),menu:""}
            app.finish(st,evt.target,true)
        }
        return 'pass'
    }
    // a checkbox activation preserves the editor bookmark instead of adopting a marker hit position.
    st = app.command(st,"edit.task",{path:path},evt.target)
    app.finish(st,evt.target,true)
    return 'prevent-default'
}
on contextmenu(evt) {
    if (st.dialog!=null or dom.closest(evt.target,"#edit-surface,.note-row")==null) { return 'pass' }
    let image = images.hit(st.editor,evt.target)
    if (image!=null) { st = {*:st,editor:model.select_image(st.editor,image)} }
    st = app.open_context(st,evt)
    app.finish(st,evt.target,true)
    return 'prevent-default'
}
on keydown(evt) {
    let key = lower(string(evt.key))
    if (st.dialog!=null) {
        if (evt.key=="Tab") {
            let card = dom.query_selector(dom.root_node(evt.target),".dialog-card")
            let fields = dom.query_selector_all(card,"input,button")
            let at = [for (i,field in fields where field==evt.target) i][0] or 0
            dom.focus_set(fields[(at+(if (evt.shiftKey) len(fields)-1 else 1))%len(fields)],false)
            return 'prevent-default'
        }
        let id = if (evt.key=="Escape") "dialog.cancel" else if (evt.key=="Enter") "dialog.confirm" else null
        if (id!=null) {
            st = app.command(st,id,{},evt.target)
            app.finish(st,evt.target)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (st.menu=="context" and contains(["ArrowUp","ArrowDown","Home","End"],evt.key)) {
        let menu = dom.get_element_by_id(dom.root_node(evt.target),"note-context-menu")
        let buttons = dom.query_selector_all(menu,"button:not([disabled])")
        let at = [for (i,button in buttons where button==evt.target) i][0] or 0
        let next = if (evt.key=="Home") 0 else if (evt.key=="End") len(buttons)-1
            else (at+len(buttons)+(if (evt.key=="ArrowUp") -1 else 1))%len(buttons)
        dom.focus_set(buttons[next],false)
        return 'prevent-default'
    }
    if (st.menu=="context" and evt.key=="Tab") {
        st = {*:st,menu:""}
        controller.focus(evt.target)
        app.finish(st,evt.target,true)
        return 'prevent-default'
    }
    if (app.primary(evt) and evt.shiftKey and key=="m" and dom.closest(evt.target,"#edit-surface,.note-row")!=null) {
        let box = dom.bounding_box(evt.target)
        st = app.open_context(st,evt,box.x+20,box.y+20)
        app.finish(st,evt.target,true)
        return 'prevent-default'
    }
    if ((st.menu=="color" or st.menu=="highlight") and evt.key=="Enter" and controller.is_control(evt.target)) {
        st = app.command(st,"picker.apply",{},evt.target)
        app.finish(st,evt.target,true)
        return 'prevent-default'
    }
    if (app.primary(evt) and key=="f") {
        dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target),"search"),false)
        return 'prevent-default'
    }
    let control = controller.is_control(evt.target)
    let command = if (app.primary(evt) and key=="n") "note.new"
        else if (app.primary(evt) and key=="s") (if (evt.shiftKey) "file.saveAs" else "file.save")
        else if (app.primary(evt) and key=="o") "file.open"
        else if (not control and app.primary(evt) and key=="b") "edit.bold"
        else if (not control and app.primary(evt) and key=="i") "edit.italic"
        else if (not control and app.primary(evt) and key=="u") "edit.underline"
        else if (not control and app.primary(evt) and key=="z") (if (evt.shiftKey) "edit.redo" else "edit.undo")
        else if (not control and app.primary(evt) and key=="y") "edit.redo"
        else if (not control and app.primary(evt) and evt.shiftKey and key=="l") "edit.checklist"
        else if (not control and app.primary(evt) and evt.key=="Enter") "edit.task"
        else null
    if (command!=null) {
        st = app.command(app.adopt(st,evt),command,{},evt.target)
        app.finish(st,evt.target,starts_with(command,"edit.") or command=="note.new")
        return 'prevent-default'
    }
    if (evt.key=="Escape") {
        let popup = st.menu!=""
        st = model.reconcile({*:st,menu:"",query:""})
        if (popup) { controller.focus(evt.target) }
        app.finish(st,evt.target,popup or not control)
        return 'prevent-default'
    }
    if (dom.closest(evt.target,"#note-list")!=null) {
        if (contains(["ArrowDown","ArrowUp"],evt.key)) {
            st = model.navigate(st,if (evt.key=="ArrowDown") 1 else -1)
            let row = dom.get_element_by_id(dom.root_node(evt.target),"row-" ++ st.selected)
            if (row!=null) dom.scroll_into_view(row)
            return 'prevent-default'
        }
        if (evt.key=="Enter") { controller.focus(evt.target); return 'prevent-default' }
        if (evt.key=="Backspace" or evt.key=="Delete") {
            st = app.command(st,"note.delete",{},evt.target)
            app.finish(st,evt.target)
            return 'prevent-default'
        }
    }
    return 'pass'
}
on closerequest(evt) {
    st = app.command(st,"file.close",{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}

<html lang:"en",<head <meta charset:"utf-8"><title "Notes"><style CSS>>apply(<notes_app>,{mode:"edit"})>
