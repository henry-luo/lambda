// dom.set_canvas_pixels_rect (Radiant_Design_Paint_Engine PE7): a rectangle write changes only
// its pixels and, on a simply placed canvas, repaints only the area it maps to.
import dom
let WHITE = as_ubyte([for (y in 0 to 7) [for (x in 0 to 7) [255, 255, 255, 255]]])
fn patch(w: int, h: int, c) => as_ubyte([for (y in 1 to h) [for (x in 1 to w) c]])
view <rect_probe> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "fixture")
    dom.set_canvas_pixels(dom.get_element_by_id(owner, "canvas"), WHITE)^
    dom.set_canvas_pixels(dom.get_element_by_id(owner, "turned"), WHITE)^
    return 'handled'
}
on click(evt) {
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "fixture")
    let canvas = dom.get_element_by_id(owner, "canvas")
    if (dom.closest(evt.target, "#patch") != null) {
        dom.set_canvas_pixels_rect(canvas, patch(2, 2, [255, 0, 0, 255]), [2, 2, 4, 4])^
    }
    if (dom.closest(evt.target, "#edge") != null) {
        dom.set_canvas_pixels_rect(canvas, patch(1, 8, [0, 0, 255, 255]), [7, 0, 8, 8])^
    }
    if (dom.closest(evt.target, "#turn") != null) {
        dom.set_canvas_pixels_rect(dom.get_element_by_id(owner, "turned"), patch(8, 1, [0, 128, 0, 255]), [0, 3, 8, 4])^
    }
    if (dom.closest(evt.target, "#reject") != null) {
        var rejected = 0
        // size mismatch, outside the bitmap, negative origin, not a rect
        dom.set_canvas_pixels_rect(canvas, patch(2, 2, [0, 0, 0, 255]), [0, 0, 3, 2]) ^ { rejected = rejected + 1 };
        dom.set_canvas_pixels_rect(canvas, patch(2, 2, [0, 0, 0, 255]), [7, 7, 9, 9]) ^ { rejected = rejected + 1 };
        dom.set_canvas_pixels_rect(canvas, patch(2, 2, [0, 0, 0, 255]), [-1, 0, 1, 2]) ^ { rejected = rejected + 1 };
        dom.set_canvas_pixels_rect(canvas, patch(2, 2, [0, 0, 0, 255]), [0, 0]) ^ { rejected = rejected + 1 };
        dom.set_attribute(owner, "data-rejected", string(rejected))
    }
    return 'handled'
}
// a pointer moving over the canvas paints the bitmap pixel under it green, as a brush stroke
// would; moves after the first change no hover state, so each repaints one small region.
on pointermove(evt) {
    let canvas = dom.closest(evt.target, "#canvas")
    if (canvas == null) { return 'pass' }
    let box = dom.bounding_box(canvas)
    let x = int(floor((evt.clientX - box.x) / box.width * 8.0))
    let y = int(floor((evt.clientY - box.y) / box.height * 8.0))
    dom.set_canvas_pixels_rect(canvas, patch(1, 1, [0, 160, 0, 255]), [x, y, x + 1, y + 1])^
    return 'handled'
}
<html <head <style "body{margin:0;background:#fff}canvas{display:block;image-rendering:pixelated}
    #canvas{position:absolute;left:20px;top:20px;width:160px;height:160px}
    #turned{position:absolute;left:240px;top:20px;width:160px;height:160px;transform:rotate(90deg)}
    #controls{position:absolute;left:20px;top:220px}">>
    apply(<rect_probe rendered:<body id:"fixture",<canvas id:"canvas",width:"8",height:"8">
        <canvas id:"turned",width:"8",height:"8">
        <div id:"controls", <button id:"patch","Patch"> <button id:"edge","Edge"> <button id:"turn","Turned">
            <button id:"reject","Reject">>>>)
>
