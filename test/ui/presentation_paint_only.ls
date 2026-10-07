// Presentation writes of opacity, or of a transform that stays a transform,
// commit without layout; paint and hit testing still follow the new values.
import dom

view <paint_probe> {
    <div id: "probe", style: "position:relative;width:600px;height:200px;",
        <button id: "fade", "Fade">
        <button id: "toggle", "Toggle">
        <button id: "move", "Move">
        <div id: "box", style: "position:absolute;left:20px;top:80px;width:60px;height:40px;background:#3b82f6;", "box">
        <div id: "moved", style: "position:absolute;left:200px;top:80px;width:60px;height:40px;background:#16a34a;transform:translate(0px,0px);", "moved">
    >
}
on click(evt) {
    let probe = dom.closest(evt.target, "#probe")
    if (probe == null) { return 'pass' }
    let id = dom.get_attribute(evt.target, "id")
    if (id == "fade") dom.presentation_style_set_property(dom.get_element_by_id(probe, "box"), "opacity", "0.5")
    else if (id == "toggle") dom.presentation_style_set_property(dom.get_element_by_id(probe, "box"), "transform", "translate(10px,0px)")
    else if (id == "move") dom.presentation_style_set_property(dom.get_element_by_id(probe, "moved"), "transform", "translate(100px,0px)")
    else dom.set_attribute(probe, "data-hit", id)
    return 'handled'
}

<html <body style: "margin:0;", apply(<paint_probe>)>>
