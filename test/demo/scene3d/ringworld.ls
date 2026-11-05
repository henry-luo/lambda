// visual demo: open with ./lambda.exe view test/demo/scene3d/ringworld.ls
import s: lambda.scene3d
import geometry: ._geometry
import navigation: ._navigation
import dom
import camera: lambda.scene3d.camera
import viewer: lambda.scene3d.viewer
import runtime: lambda.scene3d.runtime
import scene_dom: lambda.scene3d.dom

let pi = 3.141592653589793

fn planet_color(latitude, longitude, rows, columns) array {
    let theta = pi * latitude / rows
    let phi = 2.0 * pi * longitude / columns
    // a longitude-dependent storm makes the planet's rotation visible.
    let storm = exp(0.0 - (((theta - 1.8) / 0.15) ** 2 + ((phi - 0.95) / 0.32) ** 2))
    let stripe = sin(theta * 25.0 + 0.5 * sin(phi * 3.0) + 2.0 * storm)
    let detail = 0.035 * sin(theta * 93.0 + cos(phi * 7.0))
    let color = if (stripe > 0.5) [0.98, 0.77, 0.52]
        else if (stripe < -0.5) [0.62, 0.31, 0.23]
        else [0.88, 0.53, 0.34];
    [for (i in 0 to 2, let c = color[i] * (1.0 - storm * 0.55) + detail)
        min(1.0, max(0.0, c + (if (i == 0) storm * 0.35 else 0.0)))]
}

fn sphere(id, rows, columns, banded) element^ {
    let colors = if (banded) [for (j in 0 to rows) for (i in 0 to columns)
        *planet_color(j, i, rows, columns)] else null;
    geometry.sphere(id, rows, columns, colors)
}

fn ring(inner, outer, color, options = {}) element^ {
    let segments = 144
    let positions = [for (i in 0 to segments) for (r in [inner, outer])
        *[r * cos(2.0 * pi * i / segments), 0.0, r * sin(2.0 * pi * i / segments)]]
    let normals = [for (i in 0 to segments) *[0.0, 1.0, 0.0, 0.0, 1.0, 0.0]]
    let indices = [for (i in 0 to (segments - 1), let a = i * 2)
        *[a, a + 2, a + 1, a + 1, a + 2, a + 3]];
    s.mesh(s.geometry(positions, {normals: normals, indices: indices}),
        s.material('lambert', {color: color, side: 'double'}), options)
}

fn moon(position, radius, material) element => <mesh geometry: "moon", material: material,
    position: position, scale: [radius, radius, radius]>

fn orbit_track(id, turns) element^ {
    let steps = turns * 4;
    s.track(id ++ ".quaternion", [for (i in 0 to steps) 48.0 * i / steps],
        [for (i in 0 to steps) *[0.0, sin(pi * i / 4.0), 0.0, cos(pi * i / 4.0)]], 'quaternion')
}

fn control(id, label, title) element => <button id: id, type: "button", title: title, 'aria-label': title, label>

fn home() map^ => camera.prepare({position:[7.5,4.8,11.0],target:[0.0,0.0,0.0],fov:42.0},1040,570)
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"ringworld-page")
pn button_state(owner,id,value,label) {
    let button=dom.query_selector(owner,"#"++id)
    scene_dom.attr(button,"aria-pressed",string(value))^
    if (label != null) dom.set_text_content(button,label)
}

view <ring_session> state playing:true,touring:false,token:0,last_ms:null,tour_time:0.0,tour_base:null { ~.rendered }
on ring_frame(evt) {
    if ((evt.event_phase != 2 or evt.detail != token or not touring)^) { return 'pass' }
    let dt = if (last_ms == null) 0.0 else min(0.1,max(0.0,(evt.time_stamp-last_ms)/1000.0))
    tour_time = tour_time+dt
    let phase=tour_time*math.pi/12.0
    let delta=camera.subtract(tour_base.position,tour_base.target)
    let distance=sqrt(camera.dot(delta,delta))
    let azimuth=math.atan2(delta[0],delta[2])+phase
    let polar=min(math.pi-0.08,max(0.08,math.acos(delta[1]/distance)+0.32*sin(phase)))
    let radius=min(38.0,max(7.0,distance*(1.0-0.18*sin(phase))))
    let target=camera.add(tour_base.target,[0.8*sin(phase),0.3*sin(phase*2.0),0.0])
    let position=camera.add(target,[radius*sin(polar)*sin(azimuth),radius*cos(polar),radius*sin(polar)*cos(azimuth)])
    viewer.set_camera(evt.target,"main",{*:tour_base,position:position,target:target})^
    last_ms = evt.time_stamp
    token = dom.request_frame(evt.target,"ring_frame")
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"button")
    if (button == null) { return 'pass' }
    let action = dom.get_attribute(button,"id")
    let owner = owner_of(evt)
    let scene = dom.query_selector(owner,"#ringworld")
    if (action == "auto-play") {
        touring = not touring
        if (touring) {
            tour_base=viewer.snapshot(owner,"main",home()^)
            tour_time=0.0; token = dom.request_frame(owner,"ring_frame"); last_ms = null
        }
        else if (token > 0) { dom.cancel_frame(owner,token); token = 0 }
        button_state(owner,"auto-play",touring,if (touring) "Stop Auto Play" else "Auto Play")^
        return 'handled'
    }
    if (action == "play") {
        playing = not playing
        if (playing) { runtime.play(scene)^ } else { runtime.pause(scene)^ }
        button_state(owner,"play",playing,if (playing) "Pause" else "Play")^
        return 'handled'
    }
    if (action == "rewind") {
        runtime.seek(scene,0.0)^
        if (playing) { runtime.play(scene)^ }
        return 'handled'
    }
    {
        if (token > 0) { dom.cancel_frame(owner,token); token=0 }
        touring=false
        button_state(owner,"auto-play",false,"Auto Play")^
        let mapped = if (action == "orbit-left") "left" else if (action == "orbit-right") "right"
            else if (action == "tilt-up") "up" else if (action == "tilt-down") "down"
            else if (action == "zoom-in") "in" else if (action == "zoom-out") "out"
            else action
        if (contains(["orbit-mode","pan-mode","left","right","up","down","in","out","reset","top","side"],mapped)) {
            if (mapped == "reset") viewer.command(owner,"main","orbit-mode")^
            viewer.command(owner,"main",mapped)^
            if (mapped == "orbit-mode" or mapped == "pan-mode" or mapped == "reset") {
                button_state(owner,"orbit-mode",mapped != "pan-mode",null)^
                button_state(owner,"pan-mode",mapped == "pan-mode",null)^
            }
        }
    }
    return 'handled'
}
on pointerdown(evt) {
    if (dom.closest(evt.target,"scene3d") == null or not touring) { return 'pass' }
    if (token > 0) dom.cancel_frame(owner_of(evt),token)
    token=0; touring=false; last_ms=null
    button_state(owner_of(evt),"auto-play",false,"Auto Play")^
    return 'pass'
}
on wheel(evt) {
    if (dom.closest(evt.target,"scene3d") == null or not touring) { return 'pass' }
    if (token > 0) dom.cancel_frame(owner_of(evt),token)
    token=0; touring=false; last_ms=null
    button_state(owner_of(evt),"auto-play",false,"Auto Play")^
    return 'pass'
}
on keydown(evt) {
    if (dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    if (evt.key == " ") {
        let owner=owner_of(evt)
        playing=not playing
        if (playing) { runtime.play(dom.query_selector(owner,"#ringworld"))^ }
        else { runtime.pause(dom.query_selector(owner,"#ringworld"))^ }
        button_state(owner,"play",playing,if (playing) "Pause" else "Play")^
        return 'prevent-default'
    }
    if ((touring and contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","+","=","-","_","r","R"],evt.key))^) {
        if (token > 0) dom.cancel_frame(owner_of(evt),token)
        token=0; touring=false; last_ms=null
        button_state(owner_of(evt),"auto-play",false,"Auto Play")^
    }
    return 'pass'
}
on closerequest(evt) {
    if (token > 0) dom.cancel_frame(owner_of(evt),token)
    token=0; touring=false
    button_state(owner_of(evt),"auto-play",false,"Auto Play")^
    return 'pass'
}

let stars = [for (i in 0 to 159) {
    // deterministic scatter gives a stable composition without an external asset.
    let x = 28.0 * ((i * 73 % 163) / 163.0 - 0.5)
    let y = 19.0 * ((i * 47 % 167) / 167.0 - 0.5)
    let z = -9.0 - (i % 7)
    let radius = if (i % 13 == 0) 0.036 else 0.016;
    s.transform([x, y, z], [0.0, 0.0, 0.0], [radius, radius, radius])^
}]

let viewport = s.normalize(s.scene([
    s.camera({id: "main", position: [7.5, 4.8, 11.0], target: [0.0, 0.0, 0.0], fov: 42.0}),
    s.light('ambient', {color: "#b5c7ef", intensity: 0.18}),
    s.light('directional', {position: [-7.0, 9.0, 6.0], color: "#fff0d8", intensity: 1.15}),
    s.light('directional', {position: [5.0, -2.0, -5.0], color: "#729bff", intensity: 0.38}),
    s.resources([
        sphere("planet", 56, 96, true),
        sphere("moon", 20, 32, false),
        s.box([1.0, 1.0, 1.0], {id: "star"}),
        s.material('lambert', {id: "surface", color: "#ffffff"}),
        s.material('lambert', {id: "ice", color: "#74ded3"}),
        s.material('lambert', {id: "dust", color: "#dfbda4"}),
        s.material('lambert', {id: "violet", color: "#aaa1e5"}),
        s.material('basic', {id: "starlight", color: "#b6cbe3"})
    ]),
    <mesh geometry: "star", material: "starlight", instances: stars>,
    s.group([
        <mesh id: "planet-spin", geometry: "planet", material: "surface", scale: [2.1, 1.95, 2.1]>,
        ring(2.62, 2.76, "#635b61"),
        ring(2.80, 3.12, "#b79b82"),
        ring(3.15, 3.23, "#e0c39a"),
        ring(3.26, 3.73, "#b69474"),
        ring(3.77, 3.87, "#e8cfa4"),
        ring(3.99, 4.13, "#7e746f"),
        ring(4.17, 4.24, "#b69b82")
    ], {rotation: [0.14, 0.0, -0.27]}),
    s.group([
        ring(5.22, 5.235, "#315466"),
        s.group([moon([-4.65, 0.0, 2.4], 0.54, "ice")], {id: "ice-orbit"})
    ], {rotation: [-0.16, 0.0, 0.13]}),
    s.group([
        ring(6.02, 6.032, "#383c57"),
        s.group([moon([4.72, 0.0, -3.72], 0.33, "violet")], {id: "violet-orbit"})
    ], {rotation: [0.38, 0.0, -0.2]}),
    s.group([moon([4.0, 3.0, 0.5], 0.22, "dust")], {id: "dust-orbit"}),
    s.clip("orbits", [orbit_track("planet-spin", 4), orbit_track("ice-orbit", 2),
        orbit_track("violet-orbit", 1), orbit_track("dust-orbit", 3)], {duration: 48.0, autoplay: true})
], {id: "ringworld", camera: "main", width: 1040, height: 570,
    viewBox: "0 0 1040 570", background: "#080e1b",
    tabindex: "0", role: "img", 'aria-label': "Interactive ringed planet and three orbiting moons",
    'aria-describedby': "navigation-help", 'data-ready':"true",
    style: "display:block;width:100%;height:auto;touch-action:none"}))^;

<html
    <head
        <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title "Ringworld / Scene3D">
        <link rel:"stylesheet",href:"demo.css">
        <link rel: "stylesheet", href: "ringworld.css">
    >
    <body
        apply(<ring_session rendered:<main id:"ringworld-page", *[
            navigation.breadcrumbs("Ringworld"),
            <header *[
                <div *[<p class: "eyebrow", "PROCEDURAL PLANET STUDY / 001">, <h1 "RINGWORLD">]>,
                <div class: "edition", "LAMBDA + RADIANT">
            ]>,
            <nav class: "controls", 'aria-label': "Scene controls", *[
                <div class: "control-group", *[
                    <span class: "label", "PLAYBACK">,
                    <button id: "auto-play", type: "button", 'aria-pressed': "false",
                        title: "Automatically orbit, pan, tilt and zoom the camera", "Auto Play">,
                    <button id: "play", type: "button", 'aria-pressed': "true", "Pause">,
                    control("rewind", "↺", "Restart animation")
                ]>,
                <div class: "control-group", *[
                    <span class: "label", "DRAG">,
                    <button id: "orbit-mode", type: "button", 'aria-pressed': "true", "Orbit">,
                    <button id: "pan-mode", type: "button", 'aria-pressed': "false", "Pan">
                ]>,
                <div class: "control-group", *[
                    <span class: "label", "ORBIT / TILT">,
                    control("orbit-left", "←", "Orbit left"), control("orbit-right", "→", "Orbit right"),
                    control("tilt-up", "↑", "Tilt up"), control("tilt-down", "↓", "Tilt down")
                ]>,
                <div class: "control-group", *[
                    <span class: "label", "ZOOM">,
                    control("zoom-in", "+", "Zoom in"), control("zoom-out", "−", "Zoom out"),
                    control("reset", "Reset view", "Reset camera and pan"),
                    control("top", "Top", "View the rings from above"), control("side", "Side", "View the planet at its equator")
                ]>
            ]>,
            <p id: "navigation-help", "Auto Play tours the camera · Drag to orbit · Pan mode or Shift-drag to move · Scroll to zoom · Arrow keys pan · Shift + arrows tilt · + / − zoom · R resets · Space pauses">,
            viewer.interactive(viewport,home()^,null,{min_distance:7.0,max_distance:38.0,min_polar:0.08,max_polar:math.pi-0.08,pan_arrows:true},false)^,
            <div class: "caption", *[
                <div *[
                    <h2 "A small world, in motion.">,
                    <p "A rotating storm-banded atmosphere, seven tilted rings and three moons on different orbits. Every surface is generated in Lambda and lit in native 3D.">
                ]>,
                <div class: "stats", *[for (stat in [
                    {value: "03", label: "MOONS"}, {value: "07", label: "RINGS"},
                    {value: "160", label: "STARS"}])
                    <div *[<div class: "stat", stat.value>, <div class: "label", stat.label>]>
                ]>
            ]>
        ]>>)
    >
>
