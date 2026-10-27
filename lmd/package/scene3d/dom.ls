// D7.5.3v2: mount typed Scene3D descriptions at the explicit DOM boundary.
import dom
import camera: .camera

pub fn attribute(value) => if (value is array) format(value,'json') else string(value)
pub fn node(owner,id) => dom.query_selector(owner,"#" ++ id)
pub pn matching_child(parent,id,key,value) {
    var target = node(parent,id)
    // a restored ID can describe different geometry; retire its previous visual before remounting.
    if (target != null and dom.get_attribute(target,key) != attribute(value)) {
        dom.remove_child(parent,target)
        target = null
    }
    return target
}
pub pn attr(target,key,value) {
    let text = attribute(value)
    if (dom.get_attribute(target,key) != text) dom.set_attribute(target,key,text)
}
pub pn text(owner,id,value) {
    let target = node(owner,id)
    if (dom.text_content(target) != value) dom.set_text_content(target,value)
}
pub pn show(owner,id,visible,display="block") {
    let target = node(owner,id)
    let desired = if (visible) display else "none"
    if (dom.style_get_property(target,"display") != desired) dom.style_set_property(target,"display",desired)
}
pub pn clear(parent) {
    // child_nodes is live; retain a value snapshot before removing siblings.
    let children = [for (child in dom.child_nodes(parent)) child]
    for (child in children) dom.remove_child(parent,child)
}
pub pn replace(parent,trees) {
    clear(parent)
    for (tree in trees) mount(parent,tree)
}
pub pn measured_camera(viewport,options) {
    let bounds = dom.bounding_box(viewport)
    return {camera:camera.prepare(options,max(1.0,bounds.width),max(1.0,bounds.height)),bounds:bounds}
}
pub pn camera_sync(target,options) {
    for (key in ["position","target","up","fov"] where options[key] != null) attr(target,key,options[key])
}

// HTML formatting omits array attributes; preserve geometry and transform components.
pub pn mount(parent,tree,texture_size=null) {
    let node = dom.create_element(dom.owner_document(parent),string(name(tree)))
    for (key,value in tree where key is string or key is symbol)
        dom.set_attribute(node,string(key),attribute(value))
    if (name(tree) == 'texture' and texture_size != null) dom.set_attribute(node,"max-size",string(texture_size))
    for (child in content(tree)) mount(node,child,texture_size)
    dom.append_child(parent,node)
    return node
}
