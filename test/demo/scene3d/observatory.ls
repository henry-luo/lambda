// Kinetic Observatory: the document supplies the scene; Radiant owns its native clock and GPU resources.
import s: lambda.scene3d
import camera: lambda.scene3d.camera
import viewer: lambda.scene3d.viewer
import runtime: lambda.scene3d.runtime
import scene_dom: lambda.scene3d.dom
import dom
import mesh: .skymaster.mod_mesh
import geometry: ._geometry
import navigation: ._navigation

let TIMES=[0.0,2.0,4.0,6.0,8.0]
fn home() map^ => camera.prepare({position:[8.0,6.5,10.0],target:[0.0,1.25,0.0],fov:40.0},800,500)
fn spin(path,rate) element^ => s.track(path++".quaternion",TIMES,
    [for (t in TIMES) *[0.0,sin(rate*t/2.0),0.0,cos(rate*t/2.0)]],'quaternion')
fn satellite(i) => (let a=i*math.pi*0.4,
    [cos(a)*2.7,1.9+sin(a)*0.55,sin(a)*2.7])
let RING=mesh.torus([0.0,0.0,0.0],[0.0,0.0,1.0],1.65,0.085,0.085,80,12)
let PLATFORM=mesh.cylinder([0.0,-0.14,0.0],[0.0,0.14,0.0],3.8,80,3.6)
let model=s.scene([
    s.camera({id:"observatory-camera",position:[8.0,6.5,10.0],target:[0.0,1.25,0.0],fov:40.0}),
    s.environment('room',{id:"observatory-room"})^,
    s.light('hemisphere',{color:"#89b9db",'ground-color':"#312817",intensity:0.8,position:[0.0,1.0,0.0]}),
    s.light('directional',{id:"sun",color:"#ffdda1",intensity:2.2,position:[4.0,7.0,3.0],target:[0.0,0.0,0.0],
        'cast-shadow':true,'shadow-map-size':512,'shadow-extent':5.0,'shadow-normal-bias':0.04}),
    s.resources([
        mesh.geometry("observatory-ring",RING,[for (p in RING.p)
            *[math.atan2(p[1],p[0])/(2.0*math.pi)+0.5,
              math.atan2(p[2],sqrt(p[0]*p[0]+p[1]*p[1])-1.65)/(2.0*math.pi)+0.5]])^,
        mesh.geometry("observatory-rim",mesh.torus([0.0,0.0,0.0],[0.0,1.0,0.0],3.55,0.028,0.028,80,8))^,
        mesh.geometry("observatory-platform",PLATFORM,[for (p in PLATFORM.p) *[p[0]/7.6+0.5,p[2]/7.6+0.5]])^,
        geometry.sphere("observatory-sphere",20,28)^,
        s.texture("assets/brushed-normal.png",{id:"brushed-normal"}),
        s.texture("assets/stone.jpg",{id:"stone-texture"}),
        s.material('standard',{id:"metal",color:"#67c9ba",metalness:0.9,roughness:0.23,'normal-texture':"brushed-normal"}),
        s.material('standard',{id:"gold",color:"#e1a65c",metalness:0.82,roughness:0.22}),
        s.material('standard',{id:"stone",color:"#e5d7c0",roughness:0.86,texture:"stone-texture"})
    ]),
    s.group([
        s.group([<mesh id:"outer",geometry:"observatory-ring",material:"metal",'cast-shadow':true>],{rotation:[0.45,0.25,0.45]}),
        s.group([<mesh id:"inner",geometry:"observatory-ring",material:"gold",scale:[0.76,0.76,0.76],'cast-shadow':true>],{rotation:[-0.8,0.2,-0.4]}),
        <mesh geometry:"observatory-sphere",material:"metal",scale:[0.62,0.62,0.62],'cast-shadow':true>,
        for (i in 0 to 4,let p=satellite(i)) <mesh id:"satellite-"++string(i+1),geometry:"observatory-sphere",
            material:if (i%2 == 0) "gold" else "metal",position:[p[0],p[1]-1.7,p[2]],scale:[0.23,0.23,0.23],'cast-shadow':true>
    ],{id:"mechanism",position:[0.0,1.7,0.0]}),
    <mesh geometry:"observatory-platform",material:"stone",'cast-shadow':true,'receive-shadow':true>,
    <mesh geometry:"observatory-rim",material:"gold",position:[0.0,0.16,0.0]>,
    s.mesh(s.box([200.0,0.05,200.0]),s.material('standard',{color:"#132b32",roughness:0.8}),
        {position:[0.0,-0.18,0.0],'receive-shadow':true})^,
    s.postprocessing([s.post_effect('fxaa')]),
    s.clip("orbits",[spin("outer",math.pi/4.0)^,spin("inner",math.pi/4.0)^,spin("mechanism",math.pi/4.0)^,
        s.track("sun.position",TIMES,[for (t in TIMES) *[4.0*cos(t*math.pi/4.0),7.0,3.0+3.0*sin(t*math.pi/4.0)]],'vector')],
        {duration:8.0,autoplay:true})
],{id:"observatory",camera:"observatory-camera",width:800,height:500,viewBox:"0 0 800 500",
    background:"#07121c",environment:"observatory-room",'tone-mapping':'aces',exposure:1.1,
    tabindex:"0",role:"img",'aria-label':"Interactive kinetic observatory",
    style:"display:block;width:100%;max-width:800px;height:auto;touch-action:none"});

fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"observatory-page")
// Project the animated satellite centers using the same camera and native scene time.
pn selected(owner,x,y) {
    let scene=dom.query_selector(owner,"#observatory")
    let rect=dom.bounding_box(scene)
    let lens=viewer.snapshot(owner,"observatory-camera",home()^)
    let lens_view=camera.prepare(lens,rect.width,rect.height)^
    let snapshot=runtime.snapshot(scene)^
    let theta=snapshot.time*math.pi/4.0
    let hits=[for (i in 0 to 4,let p=satellite(i),
        let q=[p[0]*cos(theta)+p[2]*sin(theta),p[1],p[2]*cos(theta)-p[0]*sin(theta)],
        let projected=camera.project(lens_view,q),let dx=x-rect.x-projected.x,let dy=y-rect.y-projected.y,
        let radius=max(7.0,0.23*rect.height/(2.0*projected.depth*lens_view.tangent))
        where projected.visible and dx*dx+dy*dy <= radius*radius) {index:i+1,depth:projected.depth}]
    return if (len(hits)>0) "Satellite "++string(hits[0].index) else null
}
view <observatory_session> state playing:false,down:null { ~.rendered }
on observatory_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    runtime.pause(dom.query_selector(evt.target,"#observatory"))^
    scene_dom.attr(dom.query_selector(evt.target,"#status"),"data-ready","true")^
    return 'handled'
}
on click(evt) {
    let button=dom.closest(evt.target,"#play")
    if (button == null) { return 'pass' }
    let scene=dom.query_selector(owner_of(evt),"#observatory")
    playing=not playing
    if (playing) { runtime.play(scene)^ } else { runtime.pause(scene)^ }
    scene_dom.attr(button,"aria-pressed",string(playing))^
    dom.set_text_content(button,if (playing) "Pause" else "Play")
    return 'handled'
}
on pointerdown(evt) {
    if (dom.closest(evt.target,"#observatory") == null or evt.button != 0) { return 'pass' }
    down={x:evt.clientX,y:evt.clientY}
    return 'pass'
}
on pointerup(evt) {
    if (down == null) { return 'pass' }
    let dx=evt.clientX-down.x,dy=evt.clientY-down.y
    if (dx*dx+dy*dy < 25.0) {
        let owner=owner_of(evt),hit=selected(owner,evt.clientX,evt.clientY)^
        let status=dom.query_selector(owner,"#status")
        scene_dom.attr(status,"data-selection",if (hit == null) "none" else hit)^
        dom.set_text_content(status,if (hit == null) "Drag to orbit · scroll to zoom" else hit++" selected")
    }
    down=null
    return 'pass'
}
on pointercancel(evt) { down=null; return 'pass' }
view <observatory_document> { ~.rendered }
on load(evt) {
    let owner=dom.get_element_by_id(evt.target,"observatory-page")
    if (owner != null) dom.request_frame(owner,"observatory_activate")
    return 'handled'
}
<html <head <meta name:"viewport",content:"width=device-width,initial-scale=1"><meta charset:"utf-8">
    <title "Kinetic observatory / Scene3D"><link rel:"stylesheet",href:"demo.css"><link rel:"stylesheet",href:"observatory.css">>
    apply(<observatory_document rendered:<body apply(<observatory_session rendered:<main id:"observatory-page",class:"scene3d-page",
        navigation.breadcrumbs("Kinetic observatory");
        <header <h1 "Kinetic observatory"><p "Brushed metal, warm stone and orbiting light. Drag to explore; click a satellite.">>
        viewer.interactive(model,home()^,null,{min_distance:6.0,max_distance:24.0,max_polar:math.pi*0.47},false)^;
        <footer <button id:"play",type:"button",'aria-pressed':"false","Play"> <span id:"status",'data-ready':"false",
            "PBR environment · normal map · stone · moving shadows · FXAA">>
    >>)>>)
>
