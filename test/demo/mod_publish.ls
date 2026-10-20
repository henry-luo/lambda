// Minimal-write DOM publishing shared by the demos: each helper writes only when the
// requested value differs, so republishing an unchanged state causes no restyle (D4.5.1v4).
import dom

pub pn attr(target,key,value) {
    let requested = string(value)
    if (dom.get_attribute(target,key) != requested) dom.set_attribute(target,key,requested)
}
pub pn text(owner,id,value) {
    let target = dom.get_element_by_id(owner,id)
    if (dom.text_content(target) != value) dom.set_text_content(target,value)
}
pub pn style(target,key,value) {
    if (dom.style_get_property(target,key) != value) dom.style_set_property(target,key,value)
}
pub pn show(owner,id,visible,display="block") {
    style(dom.get_element_by_id(owner,id),"display",if (visible) display else "none")
}
// replaces a region's children when its markup changed; the last markup is kept on the element
pub pn markup(target,html) {
    if (dom.get_attribute(target,"data-markup") != html) {
        dom.set_inner_html(target,html)
        dom.set_attribute(target,"data-markup",html)
    }
}
