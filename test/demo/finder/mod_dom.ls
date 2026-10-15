import dom

pub pn attr(target,key,value) {
    let requested = string(value)
    if (dom.get_attribute(target,key) != requested) dom.set_attribute(target,key,requested)
}
