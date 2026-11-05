import collection: ~~._navigation
import dom
import physics: lambda.physics.scene
import viewer: lambda.scene3d.viewer
import camera: lambda.scene3d.camera
import scene_dom: lambda.scene3d.dom
import numbers: ~~.~~.mod_number_text
import control: ._control

pub let demos = [
    {id:"domino",title:"Domino circuit",caption:"One nudge. Thirty-two collisions.",file:"domino.ls",tag:"CHAIN REACTION"},
    {id:"salvage",title:"Salvage bay",caption:"Compound shapes, drifting in zero gravity.",file:"salvage.ls",tag:"MASS & INERTIA"},
    {id:"sorter",title:"Marble sorter",caption:"A rotating paddle and three live sensor gates.",file:"sorter.ls",tag:"KINEMATICS & SENSORS"},
    {id:"bounce",title:"Bounce lab",caption:"Five materials. One drop. Compare the traces.",file:"bounce.ls",tag:"SIMULATION & ANALYSIS"}
]

pub fn head(title) element => <head <meta charset:"utf-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
    <title title++" / Scene3D"> <link rel:"stylesheet",href:"../demo.css"> <link rel:"stylesheet",href:"physics.css">>

pub fn navigation(active) element => <nav class:"navigation",'aria-label':"Physics demonstrations",
    <a href:"../index.html", "Scene3D">
    for (demo in demos) <a href:demo.file,class:(if (active == demo.id) "active" else ""),demo.title>>

// each experiment returns directly to the shared Scene3D collection.
pub fn header(title, active) element => <header class:"page-header",
    collection.breadcrumbs(title, "../index.html");
    <div class:"header-meta", <p class:"eyebrow", "LAMBDA PHYSICS / EXPERIMENTS">
        <span class:"edition", "LAMBDA / NATIVE 3D">>
    <div class:"title-row", <h1 title> navigation(active)>>

fn action_button(action) element => <button id:action.id,type:"button",class:(if (action.primary == true) "primary" else ""),
    'data-request':format(action.request,'json'),'data-restart':string(action.restart == true),
    'data-play-after':string(action.play == true),action.label>

fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"physics-demo")
fn world(owner) => dom.query_selector(owner,"#world")
fn is_body(body) bool^ => (starts_with(body.id,"tile-") or starts_with(body.id,"cargo-") or
    starts_with(body.id,"marble-") or starts_with(body.id,"ball-"))^
fn length3(v) => sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2])

// Publish the simulation's native snapshot to the visible metrics and diagnostics.
pn refresh(owner,kind,playing,contacts,bins) {
    let scene=world(owner)
    let snapshot=physics.snapshot(scene)^
    let bodies=[for (body in snapshot.bodies where is_body(body)^) body]
    let fallen=len([for (body in bodies where body.position[1] < 0.5) body])
    let airborne=len([for (body in bodies where body.position[1] > 0.44) body])
    let speeds=[for (body in bodies) length3(body.velocity)]
    let fastest=if (len(speeds)>0) max(speeds) else 0.0
    let heights=[for (body in bodies) body.position[1]]
    let highest=if (len(heights)>0) max(heights) else 0.0
    let spin=sum([for (body in bodies) length3(body.angular_velocity)])/max(1,len(bodies))
    let energy=sum([for (body in bodies) 0.5*body.mass*length3(body.velocity)**2])
    let values=if (kind == "domino") [string(fallen)++" / "++string(len(bodies)),string(contacts),numbers.fixed(fastest,2)^++" m/s"]
        else if (kind == "salvage") [string(len(bodies)),numbers.fixed(spin,2)^++" rad/s",numbers.fixed(energy,2)^++" J"]
        else if (kind == "sorter") [for (bin in bins) string(len(bin))]
        else [string(airborne)++" / "++string(len(bodies)),numbers.fixed(highest,2)^++" m",numbers.fixed(energy,2)^++" J"]
    for (i,id in ["metric-primary","metric-secondary","metric-tertiary"]) scene_dom.text(owner,id,values[i])^
    scene_dom.text(owner,"clock",numbers.fixed(snapshot.time,2)^++" s")^
    scene_dom.text(owner,"play-toggle",if (playing) "Pause" else "Play")^
    scene_dom.attr(dom.query_selector(owner,"#play-toggle"),"aria-pressed",string(playing))^
    scene_dom.text(owner,"status",(if (playing) "Running" else "Paused")++" · tick "++string(snapshot.tick))^
    scene_dom.attr(scene,"data-ready","true")^
    scene_dom.attr(scene,"data-playing",string(playing))^
    scene_dom.attr(scene,"data-tick",string(snapshot.tick))^
    scene_dom.attr(scene,"data-physics-state",format(snapshot,'json'))^
    scene_dom.attr(scene,"data-bins",format(bins,'json'))^
    return snapshot
}

view <physics_session> state playing:~.playing,token:0,update_token:0,last_ms:null,contacts:0,counted:[],bins:[[],[],[]] { ~.rendered }
on physics_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    refresh(evt.target,~.kind,playing,contacts,bins)^
    if (playing) { token=dom.request_frame(evt.target,"physics_frame") }
    return 'handled'
}
on physics_frame(evt) {
    if ((evt.event_phase != 2 or evt.detail != token or not playing)^) { return 'pass' }
    if ((last_ms == null or evt.time_stamp-last_ms >= 150.0)^) {
        refresh(evt.target,~.kind,playing,contacts,bins)^
        last_ms=evt.time_stamp
    }
    token=dom.request_frame(evt.target,"physics_frame")
    return 'handled'
}
on physics_update(evt) {
    if ((evt.event_phase != 2 or evt.detail != update_token)^) { return 'pass' }
    update_token=0
    refresh(evt.target,~.kind,playing,contacts,bins)^
    return 'handled'
}
on physicscontactstart(evt) {
    contacts=contacts+1
    if (not playing and update_token == 0) { update_token=dom.request_frame(owner_of(evt),"physics_update") }
    return 'pass'
}
on physicssensorenter(evt) {
    if (~.kind != "sorter" or evt.detail == null) { return 'pass' }
    let a=evt.detail.body_a, b=evt.detail.body_b
    let gate=if (starts_with(a,"bin-")) a else if (starts_with(b,"bin-")) b else null
    let marble=if (starts_with(a,"marble-")) a else if (starts_with(b,"marble-")) b else null
    if ((gate == null or marble == null or contains(counted,marble))^) { return 'pass' }
    let index=int(slice(gate,4,len(gate)))^
    if (index < 0 or index >= 3) { return 'pass' }
    counted=[*counted,marble]
    bins=[for (i,bin in bins) if (i == index) [*bin,marble] else bin]
    if (not playing and update_token == 0) { update_token=dom.request_frame(owner_of(evt),"physics_update") }
    return 'handled'
}
// A host can drive the same native physics commands as the visible controls.
on physicsrequest(evt) {
    if (evt.event_phase != 2 or evt.detail == null) { return 'pass' }
    let owner=owner_of(evt), scene=world(owner), request=evt.detail
    if (request.operation == "reset") { contacts=0; counted=[]; bins=[[],[],[]] }
    let result=control.operate(scene,request)^
    if (request.operation == "query") { scene_dom.attr(scene,"data-query",format(result,'json'))^ }
    if (request.operation == "play") { playing=true }
    if (contains(["pause","reset","step"],request.operation)^) { playing=false }
    if (token > 0 and not playing) { dom.cancel_frame(owner,token); token=0 }
    if (playing and token == 0) { token=dom.request_frame(owner,"physics_frame"); last_ms=null }
    refresh(owner,~.kind,playing,contacts,bins)^
    return 'handled'
}
on click(evt) {
    let button=dom.closest(evt.target,"button")
    if (button == null) { return 'pass' }
    let id=dom.get_attribute(button,"id")
    let owner=owner_of(evt), scene=world(owner)
    if (id == "camera-home" or id == "camera-top") {
        viewer.command(owner,"main",if (id == "camera-home") "reset" else "top")^
        return 'handled'
    }
    if (id == "play-toggle") {
        playing=not playing
        if (playing) { physics.play(scene)^ } else { physics.pause(scene)^ }
    } else if (id == "step") {
        physics.step(scene,2)^
        playing=false
    } else if (id == "reset") {
        physics.reset(scene)^
        playing=false; contacts=0; counted=[]; bins=[[],[],[]]
    } else {
        let actions=[for (action in ~.actions where action.id == id) action]
        if (len(actions) == 0) { return 'pass' }
        let action=actions[0]
        if (action.restart == true) { physics.reset(scene)^; playing=false; contacts=0; counted=[]; bins=[[],[],[]] }
        control.operate(scene,action.request)^
        if (action.play == true) { physics.play(scene)^; playing=true }
    }
    if (token > 0 and not playing) { dom.cancel_frame(owner,token); token=0 }
    if (playing and token == 0) { token=dom.request_frame(owner,"physics_frame"); last_ms=null }
    refresh(owner,~.kind,playing,contacts,bins)^
    return 'handled'
}
on keydown(evt) {
    if (evt.key != " " or dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    let owner=owner_of(evt), scene=world(owner)
    playing=not playing
    if (playing) { physics.play(scene)^; token=dom.request_frame(owner,"physics_frame") }
    else { physics.pause(scene)^; if (token > 0) dom.cancel_frame(owner,token); token=0 }
    refresh(owner,~.kind,playing,contacts,bins)^
    return 'prevent-default'
}
on closerequest(evt) {
    if (token > 0) dom.cancel_frame(owner_of(evt),token)
    if (update_token > 0) dom.cancel_frame(owner_of(evt),update_token)
    token=0; update_token=0; playing=false
    return 'pass'
}
view <physics_document> { ~.rendered }
on load(evt) {
    let owner=dom.get_element_by_id(evt.target,"physics-demo")
    if (owner != null) dom.request_frame(owner,"physics_activate")
    return 'handled'
}

pub fn page(kind, scene, options) element^ => <html
    head(options.title);
    apply(<physics_document rendered:<body apply(<physics_session kind:kind,actions:options.actions,
        playing:scene["data-playing"] == "true",rendered:<main id:"physics-demo",
        header(options.title, kind);
        <p class:"intro",options.description>
        <div class:"toolbar",role:"group",'aria-label':"Simulation controls",
            for (action in options.actions) action_button(action);
            <span class:"toolbar-divider">
            <button id:"play-toggle",type:"button", "Pause / Play">
            <button id:"step",type:"button", "Step 1/60 s">
            <button id:"reset",type:"button", "Reset">
            <span class:"toolbar-divider">
            <button id:"camera-home",type:"button", "Overview">
            <button id:"camera-top",type:"button", "Top view">>
        <div class:"stage",viewer.interactive(scene,camera.prepare(parse(scene["data-camera"],'json')^,1040,500)^,null,{},false)^>
        <div class:"metrics",
            <div class:"metric", <span class:"metric-label",options.labels[0]> <strong id:"metric-primary", "—">>
            <div class:"metric", <span class:"metric-label",options.labels[1]> <strong id:"metric-secondary", "—">>
            <div class:"metric", <span class:"metric-label",options.labels[2]> <strong id:"metric-tertiary", "—">>
            <div class:"metric clock", <span class:"metric-label", "SIMULATION TIME"> <strong id:"clock", "0.00 s">>>
        <div class:"scene-caption", <p options.note> <p class:"hint", "Drag to orbit · Shift-drag to pan · Scroll to zoom · Space to pause">>
        if (options.chart != null) <section class:"plot-card", <h2 "The same experiment, measured"> <p options.chart_note> options.chart>;
        <p id:"status",role:"status", "Preparing experiment…">
    >>)>>)
>
