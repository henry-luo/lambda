// Retained Scene3D camera interactions through the DOM boundary (D9.5.6).
import dom
import camera: .camera
import scene_dom: .dom

fn owner_of(evt) => dom.closest(evt.target,".three-dimensional")
pn publish(owner,next,action=null) {
    scene_dom.camera_sync(dom.query_selector(owner,"camera"),next)
    // paired views can follow the live camera without rebuilding procedural geometry.
    let event=dom.get_attribute(dom.query_selector(owner,"scene3d"),"data-camera-event")
    if (event!=null) dom.dispatch(owner,{type:event,bubbles:true,detail:{camera:next,action:action}})
    return next
}
pn mode_sync(owner,mode) {
    for (id in ["orbit-mode","pan-mode"]) {
        let button=dom.query_selector(owner,"#"++id)
        if (button != null) scene_dom.attr(button,"aria-pressed",string(if (id == "pan-mode") mode == "pan" else mode == "orbit"))^
    }
}
fn orbit_move(current,yaw,pitch,options) map^ {
    let delta=camera.subtract(current.position,current.target)
    let distance=sqrt(camera.dot(delta,delta))
    let polar=math.acos(max(-1.0,min(1.0,delta[1]/distance)))
    // Preserve each viewer's polar limits without crossing the camera's up axis.
    let min_polar=options.min_polar or 0.0142
    let max_polar=options.max_polar or math.pi-0.0142
    let adjusted=max(min_polar,min(max_polar,polar+pitch))-polar;
    camera.control(current,{type:"orbit",yaw:yaw,pitch:adjusted})
}
fn moved(current,action,reset_pose,options={}) map^ {
    let delta = camera.subtract(current.position,current.target)
    let distance = sqrt(camera.dot(delta,delta))
    let home_delta = camera.subtract(reset_pose.position,reset_pose.target)
    let home_distance = sqrt(camera.dot(home_delta,home_delta));
    if (action == "reset") reset_pose
    else if (action == "left" or action == "right") orbit_move(current,if (action == "left") -0.28 else 0.28,0.0,options)
    else if (action == "up" or action == "down") orbit_move(current,0.0,if (action == "up") -0.12 else 0.12,options)
    else if (action == "top" or action == "side") camera.control(current,{type:"view",name:action})
    // zoom bounds follow the scene's home scale, including world-sized compositions.
    else camera.control(current,{type:"zoom",factor:max(options.min_distance or home_distance*0.4,
        min(options.max_distance or home_distance*1.8,distance*(if (action == "in") 0.88 else 1.0/0.88)))/distance})
}

view <orbit> state current:~.initial,drag:null,mode:"orbit" { ~.rendered }
on render(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    // a retained viewer republishes its own orbit after an enclosing author projection.
    current = publish(evt.target,current)
    'handled'
}
on camerarequest(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    current=publish(owner_of(evt),camera.prepare(evt.detail)^)
    'handled'
}
on cameracommand(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    if (evt.detail == "pan-mode" or evt.detail == "orbit-mode") {
        mode = if (evt.detail == "pan-mode") "pan" else "orbit"
        mode_sync(owner_of(evt),mode)^
        current=publish(owner_of(evt),current,evt.detail)
    } else { current=publish(owner_of(evt),moved(current,evt.detail,~.camera,~.options),evt.detail) }
    'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-camera]")
    if (button == null) { return 'pass' }
    let action=dom.get_attribute(button,"data-camera")
    if (action == "orbit-mode" or action == "pan-mode") {
        mode=if (action == "pan-mode") "pan" else "orbit"
        mode_sync(owner_of(evt),mode)^
        return 'handled'
    }
    if (action == "reset") { mode="orbit"; mode_sync(owner_of(evt),mode)^ }
    current = publish(owner_of(evt),moved(current,action,~.camera,~.options),action)
    'handled'
}
on pointerdown(evt) {
    let viewport = dom.closest(evt.target,"scene3d")
    if (viewport == null or evt.button != 0) { return 'pass' }
    drag = {x:evt.clientX,y:evt.clientY,camera:current,pan:mode == "pan" or evt.shiftKey}
    dom.focus_set(viewport,false)
    dom.set_pointer_capture(viewport,evt.pointerId)
    'prevent-default'
}
on pointermove(evt) {
    if (drag == null) { return 'pass' }
    let next = if (drag.pan) camera.control(drag.camera,{type:"pan",dx:evt.clientX-drag.x,dy:evt.clientY-drag.y})
        else orbit_move(drag.camera,(drag.x-evt.clientX)*0.008,(drag.y-evt.clientY)*0.008,~.options)
    current = publish(owner_of(evt),next)
    'prevent-default'
}
on pointerup(evt) {
    if (drag == null) { return 'pass' }
    dom.release_pointer_capture(dom.query_selector(owner_of(evt),"scene3d"),evt.pointerId)
    drag = null
    'prevent-default'
}
on pointercancel(evt) { drag = null; 'pass' }
on wheel(evt) {
    if (dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    current = publish(owner_of(evt),moved(current,if (evt.deltaY < 0) "in" else "out",~.camera,~.options))
    'prevent-default'
}
on keydown(evt) {
    let pan = ~.options.pan_arrows == true and not evt.shiftKey and contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"],evt.key)
    if (pan) {
        let delta = if (evt.key == "ArrowLeft") [-24.0,0.0] else if (evt.key == "ArrowRight") [24.0,0.0]
            else if (evt.key == "ArrowUp") [0.0,-24.0] else [0.0,24.0]
        current = publish(owner_of(evt),camera.control(current,{type:"pan",dx:delta[0],dy:delta[1]}),"pan")
        return 'prevent-default'
    }
    let action = if (evt.key == "ArrowLeft") "left" else if (evt.key == "ArrowRight") "right"
        else if (evt.key == "ArrowUp") "up" else if (evt.key == "ArrowDown") "down"
        else if (evt.key == "+" or evt.key == "=") "in" else if (evt.key == "-") "out"
        else if (lower(evt.key) == "r") "reset" else null
    if (action == null) { return 'pass' }
    if (action == "reset") { mode="orbit"; mode_sync(owner_of(evt),mode)^ }
    current = publish(owner_of(evt),moved(current,action,~.camera,~.options),action)
    'prevent-default'
}

// external toolbars reuse the same controls and retained camera controller.
pub fn controls(labels={}) element =>
    <div class:"camera-toolbar",role:"group",'aria-label':if (labels.controls==null) "3D camera controls" else labels.controls,
        title:labels.help,
        for (item in [{id:"left",title:"Orbit left"},{id:"right",title:"Orbit right"},{id:"in",title:"Zoom +"},
            {id:"out",title:"Zoom −"},{id:"reset",title:"Reset view"}])
            <button type:"button",'data-camera':item.id,if (labels[item.id]==null) item.title else labels[item.id]>>

pub fn source(scene,lens,initial=null,labels={},chrome=true) element^ {
    // camera readback must keep full precision when the author later snapshots the live pose.
    let rendered=<scene3d *:map(scene),*[for (child in ((content(scene)) ^ { [] })) if (name(child)=='camera')
        <camera *:map([for (key,value in map(child)) (key,scene_dom.attribute(value))])> else child]>;
    <orbit camera:lens,initial:(if (initial==null) lens else initial),options:labels,rendered:
    <div class:"three-dimensional",
        rendered;
        *(if (labels.after == null) [] else labels.after);
        *(if (chrome) [controls(labels),
            <p class:"orbit-help",if (labels.help==null) "Drag to orbit · Scroll to zoom · Arrow keys rotate · R resets" else labels.help>] else [])>>
}

// retain source() in a binding when a parent changes; apply it inside that author view.
pub fn interactive(scene,lens,initial=null,labels={},chrome=true) element^ => apply(source(scene,lens,initial,labels,chrome))

// promote the live camera into author state before that author replaces the scene descriptor.
pub pn snapshot(owner,id,fallback) map^ {
    let target=dom.query_selector(dom.root_node(owner),"#"++id)
    let values=map([for (key in ["position","target","up","fov"],let raw=dom.get_attribute(target,key) where raw != null)
        (key,parse(raw,'json')^)]);
    return camera.prepare({*:fallback,*:values})
}

pn camera_request(owner,id,event,detail) {
    let target=dom.query_selector(dom.root_node(owner),"#"++id)
    if (target==null) { raise error("scene viewer: camera not found") }
    dom.dispatch(dom.closest(target,".three-dimensional"),{type:event,detail:detail})
}

// external navigation must update the retained controller as well as its DOM camera.
pub pn set_camera(owner,id,next) map^ {
    let prepared=camera.prepare(next)^
    camera_request(owner,id,"camerarequest",prepared)
    return prepared
}

pub pn command(owner,id,action) {
    if (not contains(["left","right","up","down","in","out","reset","top","side","pan-mode","orbit-mode"],action)) { raise error("scene viewer: unknown camera action") }
    camera_request(owner,id,"cameracommand",action)
}
