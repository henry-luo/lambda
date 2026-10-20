import dom
let PIXELS = as_ubyte([[[255,0,0,255],[0,255,0,128]],[[0,0,255,255],[255,255,255,0]]])
view <bridge_probe> state tick:0 { ~.rendered }
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
    if (dom.closest(evt.target,"#frames") != null) {
        tick = 0
        dom.request_frame(owner,"bitmap_frame")
    }
    if (dom.closest(evt.target,"#resize") != null) {
        dom.set_canvas_pixels(canvas,as_ubyte([[[0,0,255,255],[0,0,255,255],[0,0,255,255]]]))^
    }
    if (dom.closest(evt.target,"#reject") != null) {
        var rejected = false
        dom.set_canvas_pixels(canvas,as_ubyte([[1,2],[3,4]])) ^ { rejected = true };
        dom.set_attribute(owner,"data-rejected",string(rejected))
    }
    return 'handled'
}
on bitmap_frame(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let canvas = dom.get_element_by_id(evt.target,"canvas")
    dom.set_canvas_pixels(canvas,if (tick % 2 == 0) flip(PIXELS,1) else PIXELS)^
    tick = tick+1
    if (tick<3) dom.request_frame(evt.target,"bitmap_frame")
    return 'handled'
}
<html <head <style "body{margin:0;background:#fff}canvas{display:block;width:200px;height:200px;image-rendering:pixelated}canvas[width='3']{width:300px;height:100px}">>
    apply(<bridge_probe rendered:<body id:"fixture",<canvas id:"canvas",width:"1",height:"1">
        <button id:"swap","Swap"> <button id:"frames","Frame uploads"> <button id:"resize","Resize"> <button id:"reject","Reject invalid">>>)
>
