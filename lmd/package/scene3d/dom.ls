// D7.5.3v2: mount typed Scene3D descriptions at the explicit DOM boundary.
import dom

pub fn attribute(value) => if (value is array) format(value,'json') else string(value)

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
