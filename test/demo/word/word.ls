// Native Lambda/Radiant port of WordCraft's command-driven writing workspace.
// S12.1.3 / D7.2.5: handlers own state; the body only projects the model.
import dom
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import edit_result: lambda.dom.edit_result
import session: lambda.edit.session
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui
import surface: .mod_surface
import app: .mod_app
import page: .mod_page
import review: .mod_review
import controller: lambda.edit.controller

let BASE = url_resolve(resources.entry_uri()^,".")
let CSS = input(url_resolve(BASE,"word.css"),'text')^

edit <word_app> state st:app.initial() {
    let dirty = session.is_dirty(st.file_state,st.editor.doc)
    let words = model.word_count(st.editor.doc);
    let in_table = model.table_context(st.editor) != null
    let tab = if (st.tab=="Table" and not in_table) "Home" else st.tab;
    let paper = page.settings(st.editor.doc)
    let geometry = page.geometry(st.editor.doc);
    <body id:"word", class:if (st.focused) "focus-mode" else "", ["data-dirty"]:string(dirty),
        ["data-revision"]:string(st.editor.model_revision), ["data-tab"]:tab, ["data-readonly"]:string(st.read_only),
        <style review.css(st.editor.doc)>
        <header class:"titlebar",
            <div class:"brand", <span class:"brand-mark", "W"> <span "Word"> <span class:"brand-divider"> <span class:"brand-detail", "A LAMBDA WORKSPACE">>
            <div class:"document-title", st.file_state.envelope.title; <span class:"save-indicator", if (dirty) "  •  Unsaved" else "  •  Saved">>
            <div class:"title-actions", ui.button("save","file.save","Save","↥"); ui.button("export","file.export","Export","↗")>
        >
        <div class:"ribbon-shell",
            <nav class:"tabs", ["aria-label"]:"Ribbon tabs",
                ui.button("new","file.new","New"); ui.button("open","file.open","Open");
                <span class:"tab-divider">;
                *[for (name in ["Home","Insert","Layout","Review","View",*if (in_table) ["Table"] else []]) ui.button("tab-" ++ lower(name),"ui.tab",name,null,{tab:name},tab==name,false,"tab")];
                <div class:"tab-spacer">
                ui.button("sample","file.sample","Studio example")>
            ui.ribbon(st.editor,tab,st.nav,st.read_only)
        >
        <main class:"workspace",
            <div class:"navigation-slot", style:if (st.nav and not st.focused) "display:block" else "display:none",
                ui.navigation(st.editor,st.query,st.match_index,st.match_case,st.replace_text)>
            <section id:"stage", class:"stage", ["aria-label"]:"Document workspace",
                <div class:"canvas-heading", <span "DRAFT / " ++ (if (st.read_only) "READING" else "WRITING")> <span paper.paper ++ " · " ++ paper.orientation ++ " · Continuous editing">>
                <div class:"ruler", style:"width:" ++ string(geometry.width*st.zoom) ++ "px",
                    *[for (n in 0 to 16) <span string(n)>]>
                <div id:"paper-wrap", class:"paper-wrap", style:"width:" ++ string(geometry.width*st.zoom) ++ "px; min-height:" ++ string(geometry.height*st.zoom) ++ "px;",
                    surface.surface(st.editor,st.zoom,st.read_only)>
                <div class:"canvas-footer", "A little room for your next idea.">
            >
            if (st.review and not st.focused) ui.comments(st.editor,st.show_resolved,st.read_only) else null
        >
        <footer class:"statusbar",
            <div class:"status-left", <span class:"status-dot"> <span id:"status", role:"status", st.status> <span class:"status-divider"> <span id:"word-count", string(words) ++ " words">>
            <div class:"status-right",
                ui.button("exit-focus","view.focusMode",if (st.focused) "Exit focus" else "Focus");
                <span id:"paper-label",paper.paper> ui.button("status-zoom-out","view.zoomOut","−");
                <span id:"zoom-label", string(round(st.zoom*100)) ++ "%"> ui.button("status-zoom-in","view.zoomIn","+")>
        >
        ui.dialog(st.dialog,st.file_state,st.editor,st.read_only)
    >
}
on beforeinput() { return 'pass' }
on editaction(evt) {
    let run = controller.handle(st.editor,evt,st.read_only)
    if (st.read_only or evt.input_type=="copy") { return run.result }
    st = {*:st,editor:model.clean(run.editor),status:if (not (run.result.failure is null)) "This edit is unavailable here." else "Writing"}
    dom.set_close_guard(evt.target,session.is_dirty(st.file_state,st.editor.doc))
    return run.result
}
on selectionchange(evt) {
    let adopted = app.adopt(st,evt,true)
    if (adopted.editor!=st.editor) {
        st = {*:adopted,editor:model.clean(adopted.editor)}
        controller.restore(st.editor)
    }
}
on word_command(req) {
    st = app.adopt(st,req.event)
    st = app.command(st,req.id,req.params,req.node)
    app.finish(st,req.node)
}
on edit_dialog(req) {
    st = app.command(st,"dialog." ++ req.action,{},req.node)
    app.finish(st,req.node)
}
on control_change(req) {
    if (req.action=="find.query") { st = {*:st,query:req.value,match_index:-1} }
    else if (req.action=="find.replacement") { st = {*:st,replace_text:req.value} }
    else {
        st = app.command(app.adopt(st,req.event),req.action,req.params,req.node)
        app.finish(st,req.node)
    }
}
on keydown(evt) {
    let key = lower(string(evt.key))
    let adopted = app.adopt(st,evt)
    let id = if (evt.key=="Escape") "ui.escape"
        else if (app.primary(evt) and key=="s") (if (evt.shiftKey) "file.saveAs" else "file.save")
        else if (app.primary(evt) and key=="f") "ui.find"
        else if (app.primary(evt) and key=="k" and st.dialog==null and not app.is_control(evt.target)) "ui.link"
        else if (app.primary(evt) and evt.altKey and key=="m" and st.dialog==null and not app.is_control(evt.target)) "ui.comment"
        else if (app.primary(evt) and evt.key=="Enter" and st.dialog==null and not app.is_control(evt.target)) "insert.pageBreak"
        else if (evt.key=="Tab" and st.dialog==null and not st.read_only and not app.is_control(evt.target) and model.table_context(adopted.editor)!=null)
            (if (evt.shiftKey) "table.previousCell" else "table.nextCell")
        else if (app.primary(evt) and key==" " and st.dialog==null and not app.is_control(evt.target)) "format.clear"
        else if (app.primary(evt) and st.dialog==null and not app.is_control(evt.target)) model.shortcut(key,evt.shiftKey==true)
        else null
    if (id==null) { return 'pass' }
    st = app.command(adopted,id,{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}
on closerequest(evt) {
    st = app.command(st,"file.close",{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}

<html lang:"en", <head <meta charset:"utf-8"> <title "Word — Lambda workspace"> <style CSS ++ surface.document_css>>
    apply(<word_app>,{mode:"edit"})>
