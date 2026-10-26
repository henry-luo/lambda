// S12.1.3 / D7.2.5: a retained Word-model editor keeps rich slide text and its source separate.
import dom
import lambda.editor.mod_editor
import lambda.editor.mod_doc
import controller: lambda.edit.controller
import html: lambda.edit.html
import rendering: lambda.edit.model
import surface: ~~.word.mod_surface
import insert: .mod_insert
import c: lambda.slide.common

fn has_text(node) => if (node is string) len(node)>0 else if (not (node is element) or
    name(node) in ['svg','math','canvas','audio','video','script','style']) false
    else any([for (child in content(node)) has_text(child)])
pub fn can_edit(obj) => obj!=null and (insert.settings(obj.source)==null or insert.settings(obj.source).kind=="table") and
    (obj.tag=='text' or (obj.tag=='content' and has_text(obj.source)))

pub pn place_caret(host,point,plain=false) {
    let hit = if (plain) dom.text_control_boundary_from_point(host,point.x,point.y)
        else dom.boundary_from_point(host,int(point.x),int(point.y),null)
    if (hit!=null) {
        if (plain) dom.tc_set_selection(host,hit.offset,hit.offset,"none")
        else dom.selection_collapse(dom.document_selection(host),hit.node,hit.offset)
    }
}

fn children(n) => rendering.surface_children(n,(child)=>apply(child,{template:"slide_inline_node"}))
view slide_inline_node: map {
    if (~.kind=='node' and ~.tag=='doc') <div class:"slide-inline-document",*children(~)>
    else if (~.kind=='node' and ~.tag=='p' and rendering.attr_get(~,'edit implied')==true)
        <p class:"slide-inline-implied",*children(~)>
    else if (~.kind=='node' and ~.tag in ['raw_html','html_block'])
        <span contenteditable:"false",class:"slide-inline-atom",rendering.attr_get(~,'source')>
    else surface.project(~,(child)=>apply(child,{template:"slide_inline_node"}),false,['class','dir','lang'],"slide-text-run")
}

edit <slide_inline> state editor:edit_open(node('doc',[node('p',[text("")])]),html.schema,null), draft:null {
    <div id:"rich-text-host",
            <div id:"canvas-rich-text",class:"slide-inline-surface" ++ (if (draft.object.tag=='text') " slide-text" else ""),
                contenteditable:"true",tabindex:"0",role:"textbox",'aria-label':"Edit slide text",'aria-multiline':"true",
                style:(if (draft.object.tag=='text') "font-size:" ++ c.px(draft.object.font_size) ++ ";color:" ++ draft.object.paint ++ ";" else "") ++
                    c.value(draft.object.source.style,""),
                apply(editor.doc,{template:"slide_inline_node"})>>
}
on slide_inline_begin(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    let loaded = html.import_text(format(content(evt.detail.object.source),'html'))^;
    // reuse the logical surface while advancing the replacement document's generation.
    editor = controller.replace(editor,edit_open(loaded.doc,html.schema,null))
    draft = {*:evt.detail,initial:loaded.doc,envelope:loaded.envelope,
        ready:dom.request_frame(evt.target,"slide_inline_ready")}
    return 'handled'
}
on slide_inline_ready(evt) {
    if (evt.event_phase!=2 or draft==null or evt.detail!=draft.ready) { return 'pass' }
    let host = dom.get_element_by_id(evt.target,"canvas-rich-text")
    // a staged bookmark restore would overwrite the entry click after this handler returns.
    editor = edit_mount(editor,host,"html5_subset")
    dom.focus_set(host,false)
    if (draft.point!=null) place_caret(host,draft.point)
    else {
        let selection = dom.document_selection(host)
        dom.selection_select_all_children(selection,host)
        dom.selection_collapse_to_end(selection)
    }
    return 'handled'
}
on editaction(evt) {
    if (draft==null) { return 'pass' }
    let run = controller.handle(editor,evt,false,"html5_subset","canvas-rich-text")
    if (evt.input_type!="copy") { editor = run.editor }
    return run.result
}
on selectionchange(evt) {
    if (draft==null) { return 'pass' }
    let accepted = controller.adopt(editor,evt,"html5_subset","canvas-rich-text")
    if (accepted.changed) { editor = accepted.editor }
    return 'pass'
}
on slide_inline_read(evt) {
    if (evt.event_phase!=2 or draft==null) { return 'pass' }
    let changed = editor.doc!=draft.initial
    dom.set_attribute(evt.target,"data-inline-error","")
    dom.set_attribute(evt.target,"data-inline-changed",string(changed))
    if (changed) {
        insert.fragment(html.export_text(editor.doc,draft.envelope),"html") ^ {
            dom.set_attribute(evt.target,"data-inline-error",^.message)
        } ~ {
            dom.set_attribute(evt.target,"data-inline-mark",join([for (node in ~) format(node,'mark')],"\n"))
        }
    }
    return 'handled'
}
on slide_inline_end(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    if (draft!=null) dom.cancel_frame(evt.target,draft.ready)
    draft = null
    return 'handled'
}

pub fn widget() => apply(<slide_inline>,{mode:"edit"})
