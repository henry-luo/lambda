// Presentation writes of opacity, or of a transform that stays a transform,
// commit without layout; paint and hit testing still follow the new values.
import dom

view <paint_probe> {
    <div id: "probe", style: "position:relative;width:600px;height:200px;",
        <button id: "fade", "Fade">
        <button id: "toggle", "Toggle">
        <button id: "move", "Move">
        <button id: "hide", "Hide leaf"> <button id: "show", "Show leaf"> <button id: "hide-group", "Hide group">
        <div id: "box", style: "position:absolute;left:20px;top:80px;width:60px;height:40px;background:#3b82f6;", "box">
        <div id: "moved", style: "position:absolute;left:200px;top:80px;width:60px;height:40px;background:#16a34a;transform:translate(0px,0px);", "moved">
        <div id: "leaf", style: "position:absolute;left:400px;top:80px;width:60px;height:40px;background:#ff0000;">
        <div id: "group", style: "position:absolute;left:480px;top:80px;width:60px;height:40px;", <div style:"width:60px;height:40px;background:#0000ff;">>
        <div id:"filtered", style:"position:absolute;left:20px;top:130px;width:60px;height:40px;background:white;filter:brightness(1);--dim:.5;">
        <button id:"filter-color", style:"position:absolute;top:185px;left:20px;", "Color filter">
        <button id:"filter-spatial", style:"position:absolute;top:185px;left:160px;", "Spatial filter">
        <button id:"filter-none", style:"position:absolute;top:185px;left:300px;", "Remove filter">
    >
}
on click(evt) {
    let probe = dom.closest(evt.target, "#probe")
    if (probe == null) { return 'pass' }
    let id = dom.get_attribute(evt.target, "id")
    if (id == "fade") dom.presentation_style_set_property(dom.get_element_by_id(probe, "box"), "opacity", "0.5")
    else if (id == "toggle") dom.presentation_style_set_property(dom.get_element_by_id(probe, "box"), "transform", "translate(10px,0px)")
    else if (id == "move") dom.presentation_style_set_property(dom.get_element_by_id(probe, "moved"), "transform", "translate(100px,0px)")
    else if (id == "hide" or id == "show") dom.presentation_style_set_property(dom.get_element_by_id(probe, "leaf"), "visibility", if (id == "hide") "hidden" else "visible")
    else if (id == "hide-group") dom.presentation_style_set_property(dom.get_element_by_id(probe, "group"), "visibility", "hidden")
    else if (starts_with(id, "filter-")) dom.presentation_style_set_property(dom.get_element_by_id(probe, "filtered"),
        "filter", if (id == "filter-color") "brightness(calc(var(--dim) * 1))"
            else if (id == "filter-spatial") "blur(2px)" else "none")
    else dom.set_attribute(probe, "data-hit", id)
    return 'handled'
}

<html <body style: "margin:0;", apply(<paint_probe>)>>
