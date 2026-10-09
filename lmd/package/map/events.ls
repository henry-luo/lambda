import dom
import radiant
import cameras: .camera
import interaction: .interaction
import numbers: lambda.chart.numbers
import frames: .frame

fn pixels(text) => if (text == null or text == "" or text == "auto") 0.0 else
    float(if (ends_with(text,"px")) slice(text,0,len(text)-2) else text)
pn edge(node, side) {
    pixels(dom.computed_style(node,"border-" ++ side ++ "-width")) + pixels(dom.computed_style(node,"padding-" ++ side))
}

// use the committed content box so CSS sizing and device density do not alter map zoom.
pub pn dispatch(spec, previous, evt, options = {}) {
    let direct = dom.closest(evt.target,"geomap");
    let group = if (direct==null) dom.closest(evt.target,"[data-map-host]") else null;
    let host = if (direct!=null) direct else if (group!=null) dom.query_selector(group,"geomap") else null;
    let action = if (evt.type=="click") dom.get_attribute(evt.target,"data-map-action") else null;
    let logical = if (host == null) null else radiant.box(host);
    let initial = if (previous == null) {camera:cameras.camera(spec),drag:null,hover:[],selection:[],suppress_click:false} else previous;
    let next=if (logical == null or logical.width <= 0 or logical.height <= 0) initial else {
        let left = edge(host,"left"), right = edge(host,"right"), top = edge(host,"top"), bottom = edge(host,"bottom");
        let width = logical.width-left-right, height = logical.height-top-bottom;
        let camera = cameras.camera({*:initial.camera,width:width,height:height});
        let cx = if (evt.clientX != null) evt.clientX else evt.x;
        let cy = if (evt.clientY != null) evt.clientY else evt.y;
        let local = if (numbers.finite_number(cx) and numbers.finite_number(cy)) radiant.local_point(host,cx,cy) else null;
        let point = if (local != null) [local[0]-left,local[1]-top] else [width/2.0,height/2.0];
        let current = {*:initial,camera:camera};
        if (camera is error or (numbers.finite_number(cx) and local==null)) initial
        else if (action!=null) {
            let next=if (action=="zoom-in") cameras.zoom_at(camera,camera.zoom+1)
                else if (action=="zoom-out") cameras.zoom_at(camera,camera.zoom-1)
                else if (action=="fit" and options.bounds!=null) cameras.fit_bounds(camera,options.bounds,if (options.padding==null) 0 else options.padding)
                else if (action=="reset") cameras.camera({*:spec,width:width,height:height}) else camera;
            if (next is error) current else {*:current,camera:next,drag:null,suppress_click:false}
        }
        else if (evt.type == "pointerdown" and (evt.button == null or evt.button == 0)) {
            dom.focus_set(host,false)
            if (evt.pointerId != null) { host.set_pointer_capture(evt.pointerId) }
            {*:current,drag:{point:point,camera:camera,pointer_id:evt.pointerId,moved:false}}
        } else if (evt.type == "pointermove" and current.drag != null and
            (current.drag.pointer_id == null or current.drag.pointer_id == evt.pointerId)) {
            let dx=point[0]-current.drag.point[0],dy=point[1]-current.drag.point[1];
            if (not current.drag.moved and dx*dx+dy*dy<9) current else
                {*:current,camera:cameras.pan(current.drag.camera,dx,dy),drag:{*:current.drag,moved:true},hover:[]}
        } else if (contains(["pointerup","pointercancel","lostpointercapture"],evt.type)) {
            if (current.drag==null or (current.drag.pointer_id!=null and current.drag.pointer_id!=evt.pointerId)) current else
                {*:current,drag:null,suppress_click:current.drag.moved or evt.type=="pointercancel"}
        } else if (evt.type=="pointermove" or evt.type=="click") {
            if (evt.type=="click" and current.suppress_click) {*:current,suppress_click:false} else {
                let queried=frames.query_displayed(host,point);
                let hits=if (queried is error) [] else queried;
                let detail={node:host,features:hits,point:point,lnglat:cameras.unproject(camera,point)};
                if (evt.type=="click") {
                    if (options.on_select!=null) { options.on_select(detail) }
                    {*:current,selection:hits,suppress_click:false}
                } else {
                    if (options.on_hover!=null) { options.on_hover(detail) }
                    {*:current,hover:hits}
                }
            }
        } else if (evt.type=="pointerleave" and current.drag==null) {*:current,hover:[]}
        else if (evt.type=="dblclick") {
            let next=cameras.zoom_at(camera,camera.zoom+(if (evt.shiftKey==true) -1 else 1),point);
            if (next is error) current else {*:current,camera:next,drag:null}
        }
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
                else if (key == "-") cameras.zoom_at(camera,camera.zoom-1)
                else if (key == "Home") cameras.camera({*:spec,width:width,height:height})
                else if (key == "Escape") camera else camera;
            {*:current,camera:next,drag:null,selection:if (key=="Escape") [] else current.selection}
        } else current
    };
    if (options.on_camera!=null and next.camera!=initial.camera) {
        options.on_camera({node:host,camera:next.camera})
    }
    next
}
