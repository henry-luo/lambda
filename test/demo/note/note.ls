// A native three-pane Notes workspace, using the Finder/Word demo conventions.
import dom
import lambda.editor.mod_editor
import controller: lambda.edit.controller
import resources: ~~.mod_resources
import app: .mod_app
import model: .mod_model
import ui: .mod_ui

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
    let run = controller.handle(st.editor,evt,st.dialog!=null or st.selected=="" or model.current(st).deleted)
    if (run.result.failure==null and evt.input_type!="copy") {
        st = model.update_editor(st,run.editor,app.stamp())
        app.finish(st,evt.target)
    }
    return run.result
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
    st = app.command(app.adopt(st,req.event),req.id,req.params,req.node)
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
    // checkbox presses must cancel native caret placement before the generated check glyph is hit.
    return if (evt.button==0 and ui.task_hit(st,evt)!=null) 'prevent-default' else 'pass'
}
on click(evt) {
    let path = ui.task_hit(st,evt)
    if (path==null) { return 'pass' }
    // a checkbox activation preserves the editor bookmark instead of adopting a marker hit position.
    st = app.command(st,"edit.task",{path:path},evt.target)
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
        st = model.reconcile({*:st,menu:"",query:""})
        app.finish(st,evt.target,not control)
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
