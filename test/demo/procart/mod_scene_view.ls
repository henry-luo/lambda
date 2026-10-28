// shared retained orbit controls and instanced scene construction (S12.1.3 / D7.5.3v2).
import dom
import drawing: lambda.procart.scene
import s: lambda.scene3d
import camera: lambda.scene3d.camera
import scene_dom: lambda.scene3d.dom
import geometry: ~~.scene3d._geometry
import solids: .mod_plant_solids

pub fn pose(position=[6.8,4.8,8.2]) map => camera.prepare({position:position,target:[0.0,2.0,0.0],fov:38.0})

// batch shared shapes across a composition while retaining semantic category selectors.
pub fn meshes(parts,options={}) array => drawing.meshes({parts:[for (i,p in parts)
    {id:string(i),geometry:p.shape,matrix:solids.matrix(p),color:p.color,opacity:1.0,
     kind:if (p.category != null) p.category else "",role:p.role}]},options)

pub fn viewport(parts,resources,options) element {
    let box = solids.bounds(if (options.fit != null) options.fit else parts)
    let center = [(box.lo[0]+box.hi[0])/2.0,box.lo[1],(box.lo[2]+box.hi[2])/2.0]
    let lens = options.camera;
    s.scene([
        s.camera({id:options.camera_id,position:lens.position,target:lens.target,fov:lens.fov}),
        s.light('ambient',{color:"#e3ecd4",intensity:0.65}),
        s.light('directional',{position:[-4.0,9.0,6.0],color:"#fff1d1",intensity:0.8}),
        s.light('directional',{position:[5.0,4.0,-5.0],color:"#c4d9d5",intensity:0.45}),
        s.resources([geometry.sphere("round",10,16),geometry.sphere("plinth",8,48),*resources]),
        <mesh geometry:"plinth",position:[0.0,-0.09,0.0],scale:[2.4,0.09,2.4],s.material('lambert',{color:"#d8d6bb"})>,
        s.group([s.group(meshes(parts),{position:[for (v in center) -v]})],{id:options.model_id,scale:[box.scale,box.scale,box.scale]})
    ],{id:options.id,camera:options.camera_id,width:760,height:540,viewBox:"0 0 760 540",background:"#f4f3e7",
        tabindex:"0",role:"img",'aria-label':options.label,style:"display:block;width:100%;height:auto;touch-action:none"})
}

fn owner_of(evt) => dom.closest(evt.target,".three-dimensional")
pn publish(owner,next) {
    scene_dom.camera_sync(dom.query_selector(owner,"camera"),next)
    return next
}
fn moved(current,action,reset_pose) map {
    let delta = camera.subtract(current.position,current.target)
    let distance = sqrt(camera.dot(delta,delta))
    let home_delta = camera.subtract(reset_pose.position,reset_pose.target)
    let home_distance = sqrt(camera.dot(home_delta,home_delta));
    if (action == "reset") reset_pose
    else if (action == "left" or action == "right") camera.control(current,{type:"orbit",yaw:if (action == "left") -0.28 else 0.28})
    // zoom bounds follow the scene's home scale, including world-sized compositions.
    else camera.control(current,{type:"zoom",factor:max(home_distance*0.4,min(home_distance*1.8,distance*(if (action == "in") 0.88 else 1.0/0.88)))/distance})
}

view <orbit> state current:~.initial,drag:null { ~.rendered }
on render(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    // a retained viewer republishes its own orbit after an enclosing author projection.
    current = publish(evt.target,current)
    'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-camera]")
    if (button == null) { return 'pass' }
    current = publish(owner_of(evt),moved(current,dom.get_attribute(button,"data-camera"),~.camera))
    'handled'
}
on pointerdown(evt) {
    let viewport = dom.closest(evt.target,"scene3d")
    if (viewport == null or evt.button != 0) { return 'pass' }
    drag = {x:evt.clientX,y:evt.clientY,camera:current}
    dom.focus_set(viewport,false)
    dom.set_pointer_capture(viewport,evt.pointerId)
    'prevent-default'
}
on pointermove(evt) {
    if (drag == null) { return 'pass' }
    current = publish(owner_of(evt),camera.control(drag.camera,
        {type:"orbit",yaw:(drag.x-evt.clientX)*0.008,pitch:(drag.y-evt.clientY)*0.008}))
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
    current = publish(owner_of(evt),moved(current,if (evt.deltaY < 0) "in" else "out",~.camera))
    'prevent-default'
}
on keydown(evt) {
    let action = if (evt.key == "ArrowLeft") "left" else if (evt.key == "ArrowRight") "right"
        else if (evt.key == "+" or evt.key == "=") "in" else if (evt.key == "-") "out"
        else if (lower(evt.key) == "r") "reset" else null
    if (action == null) { return 'pass' }
    current = publish(owner_of(evt),moved(current,action,~.camera))
    'prevent-default'
}

pub fn source(scene,lens,initial=null,labels={}) element {
    // camera readback must keep full precision when the author later snapshots the live pose.
    let rendered=<scene3d *:map(scene),*[for (child in content(scene)) if (name(child)=='camera')
        <camera *:map([for (key,value in map(child)) (key,scene_dom.attribute(value))])> else child]>;
    <orbit camera:lens,initial:(if (initial==null) lens else initial),rendered:
    <div class:"three-dimensional",
        rendered;
        <div class:"camera-toolbar",role:"group",'aria-label':if (labels.controls==null) "3D camera controls" else labels.controls,
            for (item in [{id:"left",title:"Orbit left"},{id:"right",title:"Orbit right"},{id:"in",title:"Zoom +"},
                {id:"out",title:"Zoom −"},{id:"reset",title:"Reset view"}])
                <button type:"button",'data-camera':item.id,if (labels[item.id]==null) item.title else labels[item.id]>>
        <p class:"orbit-help",if (labels.help==null) "Drag to orbit · Scroll to zoom · Arrow keys rotate · R resets" else labels.help>>>
}

// retain source() in a binding when a parent changes; apply it inside that author view.
pub fn interactive(scene,lens,initial=null,labels={}) element => apply(source(scene,lens,initial,labels))

// promote the live camera into author state before that author replaces the scene descriptor.
pub pn snapshot(owner,id,fallback) map^ {
    let target=dom.query_selector(dom.root_node(owner),"#"++id)
    let values=map([for (key in ["position","target","up","fov"],let raw=dom.get_attribute(target,key) where raw != null)
        (key,parse(raw,'json')^)]);
    return camera.prepare({*:fallback,*:values})
}
