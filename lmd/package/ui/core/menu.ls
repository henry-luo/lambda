// Shared DOM menu placement and focus policy for native demo workspaces.
import dom

// The application chooses the dismissal result and restores its own focus target.
pub fn key_action(key:string) symbol => if (key in ["ArrowUp","ArrowDown","Home","End"]) 'navigate'
    else if (key in ["Escape","Tab"]) 'dismiss' else 'pass'

view <context_frame> { ~.rendered }
on render(evt) {
    if (evt.event_phase==2) {
        fit(evt.target,~.id,~.owner)^
        if (~.autofocus) focus(evt.target,~.id,~.items)^
    }
    return 'pass'
}
pub fn frame(id,owner,rendered,autofocus=true,items="button:not([disabled])") =>
    apply(<context_frame id:id,owner:owner,rendered:rendered,autofocus:autofocus,items:items>)

// measure rendered rows before clamping so the last action stays reachable.
pub pn fit(node,id,owner) {
    let root = dom.root_node(node)
    let menu = dom.get_element_by_id(root,id)
    let box = dom.bounding_box(menu)
    let viewport = dom.bounding_box(dom.get_element_by_id(root,owner))
    // The owner may be offset inside a larger document, so clamp in its coordinates.
    dom.style_set_property(menu,"left",string(max(viewport.x+8,min(box.x,viewport.x+viewport.width-box.width-8)))^ ++ "px")
    dom.style_set_property(menu,"top",string(max(viewport.y+8,min(box.y,viewport.y+viewport.height-box.height-8))) ++ "px")
}

pub pn focus(node,id,items="button:not([disabled])") {
    let menu = dom.get_element_by_id(dom.root_node(node),id)
    dom.focus_set(dom.query_selector(menu,items),false)
}

pub pn navigate(node,id,key,items="button:not([disabled])") {
    let menu = dom.get_element_by_id(dom.root_node(node),id)
    let buttons = dom.query_selector_all(menu,items)
    if ((len(buttons)==0)^) { return }
    let at = [for (i,button in buttons where button==node) i][0] or 0
    let next = if (key=="Home") 0 else if (key=="End") len(buttons)-1
        else (at+len(buttons)+(if (key=="ArrowUp") -1 else 1))%len(buttons)
    dom.focus_set(buttons[next],true)
}
