import dom
import radiant
import cameras: .camera
import interaction: .interaction
import numbers: lambda.chart.numbers

fn pixels(text) => if (text == null or text == "" or text == "auto") 0.0 else
    float(if (ends_with(text,"px")) slice(text,0,len(text)-2) else text)
pn edge(node, side) {
    pixels(dom.computed_style(node,"border-" ++ side ++ "-width")) + pixels(dom.computed_style(node,"padding-" ++ side))
}

// use the committed content box so CSS sizing and device density do not alter map zoom.
pub pn dispatch(spec, previous, evt) {
    let host = dom.closest(evt.target,"geomap");
    let box = if (host == null) null else dom.bounding_box(host);
    let logical = if (host == null) null else radiant.box(host);
    let initial = if (previous == null) {camera:cameras.camera(spec),drag:null} else previous;
    if (box == null or logical == null or box.width <= 0 or box.height <= 0) initial else {
        let left = edge(host,"left"), right = edge(host,"right"), top = edge(host,"top"), bottom = edge(host,"bottom");
        let width = logical.width-left-right, height = logical.height-top-bottom;
        let camera = cameras.camera({*:initial.camera,width:width,height:height});
        let cx = if (evt.clientX != null) evt.clientX else evt.x;
        let cy = if (evt.clientY != null) evt.clientY else evt.y;
        let point = if (numbers.finite_number(cx) and numbers.finite_number(cy))
            [(cx-box.left)*logical.width/box.width-left,(cy-box.top)*logical.height/box.height-top]
            else [width/2.0,height/2.0];
        let current = {*:initial,camera:camera};
        if (camera is error) initial
        else if (evt.type == "pointerdown" and (evt.button == null or evt.button == 0)) {
            dom.focus_set(host,false)
            if (evt.pointerId != null) { host.set_pointer_capture(evt.pointerId) }
            {*:current,drag:{point:point,camera:camera,pointer_id:evt.pointerId}}
        } else if (evt.type == "pointermove" and current.drag != null and
            (current.drag.pointer_id == null or current.drag.pointer_id == evt.pointerId)) {
            {*:current,camera:cameras.pan(current.drag.camera,point[0]-current.drag.point[0],point[1]-current.drag.point[1])}
        } else if (contains(["pointerup","pointercancel","lostpointercapture"],evt.type)) {*:current,drag:null}
        else if (evt.type == "wheel") {
            let delta = evt.deltaY * (if (evt.deltaMode == 1) 16.0 else if (evt.deltaMode == 2) height else 1.0);
            let next = interaction.update(camera,{type:"wheel",delta_y:delta,point:point});
            if (next is error) current else {*:current,camera:next,drag:null}
        } else if (evt.type == "keydown") {
            let key = evt.key;
            let next = if (key == "ArrowLeft") cameras.pan(camera,40,0)
                else if (key == "ArrowRight") cameras.pan(camera,-40,0)
                else if (key == "ArrowUp") cameras.pan(camera,0,40)
                else if (key == "ArrowDown") cameras.pan(camera,0,-40)
                else if (key == "+" or key == "=") cameras.zoom_at(camera,camera.zoom+1)
                else if (key == "-") cameras.zoom_at(camera,camera.zoom-1) else camera;
            {*:current,camera:next,drag:null}
        } else current
    }
}
