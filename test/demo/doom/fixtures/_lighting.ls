// Native consumer probe: paused CSS time keeps pixel expectations reproducible.
import dom
import data: ~~.mod_data
let BASE = url_resolve(data.entry_uri()^, "../")
let TREE = <main id:"probe",
    <div id:"light", class:"light-glow", style:"--light:.4;animation-duration:1s;animation-timing-function:linear;animation-delay:-.25s;animation-fill-mode:both;",
        <div class:"lit">
    >
    <div id:"scroll", style:"animation:doom-scroll 3.66s linear -1.83s both paused;">
    <div id:"glow", style:"animation:doom-projectile-glow .15s linear -.075s both paused;">
    <button id:"half", style:"position:absolute;top:100px;", "Half">
    <button id:"cancel", style:"position:absolute;top:100px;left:120px;", "Cancel">
>
view <lighting_probe> { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#probe")
    if (owner == null) { return 'pass' }
    let light = dom.get_element_by_id(owner, "light")
    if (dom.get_attribute(evt.target, "id") == "half")
        dom.presentation_style_set_property(light, "animation-delay", "-.5s")
    else if (dom.get_attribute(evt.target, "id") == "cancel")
        dom.presentation_style_set_property(light, "animation-name", "none")
    return 'handled'
}
<html <head <link rel:"stylesheet", href:url_resolve(BASE, "doom.css")>
    <style "html,body{margin:0;background:black}#light{position:absolute;left:0;top:0}.lit{width:80px;height:80px;background:white;filter:brightness(var(--light))}#scroll{position:absolute;left:100px;top:0;width:calc(80px + var(--scroll-offset));height:80px;background:red}#glow{position:absolute;left:200px;top:0;width:80px;height:80px;background:rgb(40,40,40)}">>
    <body apply(<lighting_probe rendered:TREE>)>>
