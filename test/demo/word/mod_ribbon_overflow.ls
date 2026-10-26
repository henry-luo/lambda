// Word retains its ribbon lookup while sharing the collapse and menu geometry with Slides.
import dom
import overflow: ~~.mod_ribbon_overflow

fn ribbon(node) => dom.query_selector(dom.root_node(node),".ribbon-groups")

pub pn close(node) { overflow.close(ribbon(node)) }
pub pn begin_resize(node,stage) { return overflow.begin_resize(ribbon(node),stage) }
pub pn end_resize(handoff) { overflow.end_resize(handoff) }
pub pn update(node,force=false) {
    let root = dom.root_node(node)
    let word = dom.get_element_by_id(root,"word")
    overflow.update(ribbon(node),dom.get_attribute(word,"data-tab") or "",
        dom.get_attribute(word,"data-ribbon-group-labels") or "",force)
}
pub pn toggle(trigger) { overflow.toggle(trigger) }
