// Shared DOM menu placement and focus policy for native demo workspaces.
import dom

pub fn has_selection(selection) => selection.kind=='node' or selection.kind=='all' or
    (selection.kind=='text' and selection.anchor!=selection.head)

view <context_frame> { ~.rendered }
on render(evt) {
    if (evt.event_phase==2) {
        let root = dom.root_node(evt.target)
        let menu = dom.get_element_by_id(root,~.id)
        let box = dom.bounding_box(menu)
        let viewport = dom.bounding_box(dom.get_element_by_id(root,~.owner))
        // measure rendered rows before clamping so the last action stays reachable.
        dom.style_set_property(menu,"left",string(max(8,min(box.x,viewport.width-box.width-8))) ++ "px")
        dom.style_set_property(menu,"top",string(max(8,min(box.y,viewport.height-box.height-8))) ++ "px")
        if (~.autofocus) focus(evt.target,~.id)
    }
    return 'pass'
}
pub fn frame(id,owner,rendered,autofocus=true) => apply(<context_frame id:id,owner:owner,rendered:rendered,autofocus:autofocus>)

pub pn focus(node,id) {
    let menu = dom.get_element_by_id(dom.root_node(node),id)
    dom.focus_set(dom.query_selector(menu,"button:not([disabled])"),false)
}

pub pn navigate(node,id,key) {
    let menu = dom.get_element_by_id(dom.root_node(node),id)
    let buttons = dom.query_selector_all(menu,"button:not([disabled])")
    if (len(buttons)==0) { return }
    let at = [for (i,button in buttons where button==node) i][0] or 0
    let next = if (key=="Home") 0 else if (key=="End") len(buttons)-1
        else (at+len(buttons)+(if (key=="ArrowUp") -1 else 1))%len(buttons)
    dom.focus_set(buttons[next],true)
}
