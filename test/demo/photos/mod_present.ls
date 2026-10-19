import dom
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import surface: lambda.editor.raster.view
import scene: .mod_scene

pub fn node(owner,id) => dom.get_element_by_id(owner,id)
// these nodes are non-void markup fragments; the HTML document formatter adds a shell.
fn markup(nodes) => join([for (child in nodes) format(child,'xml')],"")
pub pn publish(owner,st) any^ {
    let previous_dialog = dom.get_attribute(owner,"data-dialog")
    let painted = surface.publish(owner,st)^
    dom.set_close_guard(owner,editor.dirty(st))
    let rows = editor.layers(st.doc)
    let active = if (len(st.selected)==1) editor.lookup(st.doc,st.selected[0]) else null
    for (id,requested in {status:st.status,'document-name':st.name,dirty:if (editor.dirty(st)) "●" else "",
        dimensions:string(st.doc.width) ++ " × " ++ string(st.doc.height) ++ " · RGB/8",
        zoom:if (st.zoom == 0) "Fit" else string(round(st.zoom*100)) ++ "%",
        'layer-count':string(len(rows)),'target-label':if (active == null) "Select a layer" else
            (if (st.target == "mask") "Layer mask · " else "Pixels · ") ++ active.node.name}) {
        let target = node(owner,string(id))
        if (dom.text_content(target) != requested) dom.set_text_content(target,requested)
    }
    for (key,requested in {'data-error':st.error,'data-layers':string(len(rows)),
        'data-history':string(len(st.hist.undo)),'data-selected':join(st.selected,","),'data-target':st.target,
        'data-x':if (active == null) "" else string(model.bounds(active.node).x),
        'data-y':if (active == null) "" else string(model.bounds(active.node).y),
        'data-dirty':string(editor.dirty(st)),'data-dialog':st.dialog,'data-tool':st.tool,
        'data-drag':if (st.gesture == null) "" else st.gesture.mode})
        dom.set_attribute(owner,string(key),requested)
    let layer_html = markup(scene.layer_rows(st))
    if (dom.get_attribute(node(owner,"layers"),"data-rows") != layer_html) {
        dom.set_inner_html(node(owner,"layers"),layer_html)
        dom.set_attribute(node(owner,"layers"),"data-rows",layer_html)
    }
    // focused fields keep native drafts until change; publication never interrupts typing.
    for (id,requested in {'layer-name':if (active == null) "" else active.node.name,
        opacity:if (active == null) 1 else model.value(active.node.opacity,1),
        'image-width':st.size_draft.width,'image-height':st.size_draft.height,
        'position-x':if (active == null) 0 else model.bounds(active.node).x,
        'position-y':if (active == null) 0 else model.bounds(active.node).y}) {
        let control = node(owner,string(id))
        if (not dom.matches(control,":focus") and dom.get_state(control,"value") != string(requested))
            dom.set_state(control,"value",string(requested))
    }
    for (tool in ["move","pan"]) dom.set_attribute(node(owner,"tool-" ++ tool),"class",if (st.tool == tool) "active" else "")
    for (entry in [{id:"undo",disabled:not editor.can_undo(st)^},{id:"redo",disabled:not editor.can_redo(st)^}]) {
        if (entry.disabled) dom.set_attribute(node(owner,entry.id),"disabled","")
        else if (dom.get_attribute(node(owner,entry.id),"disabled") != null) dom.remove_attribute(node(owner,entry.id),"disabled")
    }
    let history_html = markup([for (tx in reverse(st.hist.undo)) <p (tx.meta[0].value)>])
    if (dom.get_attribute(node(owner,"history"),"data-items") != history_html) {
        dom.set_inner_html(node(owner,"history"),history_html)
        dom.set_attribute(node(owner,"history"),"data-items",history_html)
    }
    dom.style_set_property(node(owner,"file-dialog"),"display",if (st.dialog in ["open","place","save","export"]) "block" else "none")
    dom.style_set_property(node(owner,"discard-dialog"),"display",if (st.dialog == "discard") "block" else "none")
    if (st.dialog == "discard" and previous_dialog != "discard") dom.focus_set(node(owner,"discard-cancel"),false)
    return painted
}
