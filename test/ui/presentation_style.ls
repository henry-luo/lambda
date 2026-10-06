import dom

view <channel> { ~.tree }
on click(evt) {
    let command = dom.get_attribute(evt.target, "id")
    let owner = dom.closest(evt.target, "#channel")
    if (owner == null) { return 'pass' }
    if (not contains(["apply", "recascade", "clear"], command)) { return 'pass' }
    let box = dom.get_element_by_id(owner, "box")
    let paint = dom.get_element_by_id(owner, "paint")
    if (command == "apply") {
        let opacity = dom.presentation_style_set_property(box, "opacity", "0.8")
        let painted = dom.presentation_style_set_property(paint, "fill", "#ff0000")
        dom.set_attribute(owner, "data-accepted", if (opacity and painted) "true" else "false")
        dom.set_attribute(owner, "data-authored-opacity", dom.style_get_property(box, "opacity"))
    } else if (command == "recascade") {
        dom.set_attribute(box, "class", "locked")
        dom.set_attribute(owner, "data-authored-opacity", dom.style_get_property(box, "opacity"))
    } else {
        dom.presentation_style_clear(box)
        dom.presentation_style_clear(paint)
        dom.remove_attribute(box, "class")
    }
    return 'handled'
}

let source = <channel tree: <div id: "channel",
    <div id: "box", style: "width:100px;height:60px;background:red;opacity:0.2;">
    <div id: "paint-region", style: "width:100px;height:60px;",
        <svg width: 100, height: 60, <rect id: "paint", width: 100, height: 60, fill: "#0000ff">>>
    <button id: "apply", "Apply"> <button id: "recascade", "Recascade"> <button id: "clear", "Clear">
>>;
<html <head <style ".locked{opacity:0.6!important}">> <body apply(source)>>
