import dom
let PIXELS = as_ubyte([[[255,0,0,255],[0,255,0,128]],[[0,0,255,255],[255,255,255,0]]])
view <bridge_probe> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"fixture")
    dom.set_canvas_pixels(dom.get_element_by_id(owner,"canvas"),PIXELS)^
    return 'handled'
}
on click(evt) {
    let owner = dom.get_element_by_id(dom.root_node(evt.target),"fixture")
    let canvas = dom.get_element_by_id(owner,"canvas")
    if (dom.closest(evt.target,"#swap") != null) {
        dom.set_canvas_pixels(canvas,flip(PIXELS,1))^
    }
    if (dom.closest(evt.target,"#reject") != null) {
        var rejected = false
        dom.set_canvas_pixels(canvas,as_ubyte([[1,2],[3,4]])) ^ { rejected = true };
        dom.set_attribute(owner,"data-rejected",string(rejected))
    }
    return 'handled'
}
<html <head <style "body{margin:0;background:#fff}canvas{display:block;width:200px;height:200px;image-rendering:pixelated}">>
    apply(<bridge_probe rendered:<body id:"fixture",<canvas id:"canvas",width:"1",height:"1">
        <button id:"swap","Swap"> <button id:"reject","Reject invalid">>>)
>
