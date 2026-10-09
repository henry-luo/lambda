import dom
import data: ~~.mod_data
import scene: ~~.mod_scene
let BASE = url_resolve(data.entry_uri()^, "../")
let TREE = <main id:"doom", style:"width:320px;margin:0;",
    scene.fuzz_filter();
    <div id:"shadow", class:"sprite", 'data-type':"spectre", 'data-state':"idle",
        style:("position:absolute;left:40px;top:20px;width:64px;height:59px;background-size:384px 413px;background-image:" ++
            scene.image_url(BASE, "assets/sprites/sheets/SARG.png") ++ ";")>
    <button id:"seed", style:"position:absolute;top:100px;", "Next seed">
    <button id:"dead", style:"position:absolute;top:100px;left:150px;", "Dead">
>
view <spectre_probe> { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#doom")
    if (dom.get_attribute(evt.target, "id") == "seed")
        dom.set_attribute(dom.get_element_by_id(owner, "fuzz-noise"), "seed", "1")
    else dom.set_attribute(dom.get_element_by_id(owner, "shadow"), "data-state", "dead")
    return 'handled'
}
<html <head <link rel:"stylesheet", href:url_resolve(BASE, "doom.css")>
    <style "html,body{background:rgb(128,128,128);margin:0}">>
    <body apply(<spectre_probe rendered:TREE>)>>
