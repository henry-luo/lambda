// Native CSS declarations must commit custom names even with a realm adapter.
import dom
view <style_probe> { ~.rendered }
on load(evt) {
    let box = dom.get_element_by_id(evt.target, "box")
    dom.style_set_property(box, "--motion", "20px")
    dom.style_set_property(box, "--size", "60px")
    dom.set_attribute(box, "data-motion", dom.style_get_property(box, "--motion"))
    dom.set_attribute(box, "data-serialized", string(contains(dom.get_attribute(box, "style"), "--motion: 20px")))
    return 'handled'
}
<html apply(<style_probe rendered:<body style:"margin:0;background:black;",
    <div id:"box", style:"position:absolute;left:10px;top:10px;--size:20px;width:var(--size);height:20px;background:red;transform:translateX(var(--motion,0px));">>
>)>
