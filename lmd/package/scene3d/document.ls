// D9.3.4/D9.5.6v2: a shipped document supplies the viewer controls and retains a native Scene3D viewport.
import dom
import paths: lambda.io.path
import asset_io: .asset
import t: .transform
import camera: .camera
import scene_dom: .dom
import environment: .environment
import runtime: .runtime
import viewer: .viewer

let DRAWABLES = ['mesh', 'points', 'line', 'line-segments']
let BACKGROUNDS = [["#f0f3f7", "Mist"], ["#ffffff", "White"], ["#d5dce6", "Slate"], ["#293545", "Charcoal"]]
let LIGHT_COLORS = [["#ffffff", "White"], ["#ffe1be", "Warm"], ["#d4e6ff", "Cool"]]
let HOME_BACKGROUND = BACKGROUNDS[0][0]

pub fn node_matrix(n) array^ => if (n.matrix is array) n.matrix
    else if (n.quaternion is array) t.compose(n.position or [0.0, 0.0, 0.0], n.quaternion, n.scale or [1.0, 1.0, 1.0])
    else t.matrix(n.position or [0.0, 0.0, 0.0], n.rotation or [0.0, 0.0, 0.0], n.scale or [1.0, 1.0, 1.0])

fn children(n) array => content(n) ^ { [] }
fn position_bounds(geometry) any^ {
    let positions = geometry.positions
    let vertex_count = if (positions is array) (len(positions)^ div 3) else 0;
    if (vertex_count == 0) null else {
        let axes = [for (axis in 0 to 2) [for (i in 0 to (vertex_count - 1)) positions[3*i + axis]]];
        {lo: [for (values in axes) min(values)^], hi: [for (values in axes) max(values)^]}
    }
}

// Cache local bounds once per shared geometry. Inline drawables retain their parent transform.
fn geometry_index(n) any^ => if (name(n) == 'geometry' and n.id != null)
    [(n.id, position_bounds(n)^)]
    else [for (child in children(n)) *geometry_index(child)^]

fn walk(n, parent, defs) any^ {
    let kind = name(n);
    if (contains(['resources', 'skeleton', 'animation-clip'], kind) or n.visible == false) []
    else if (kind == 'geometry') {
        let box = position_bounds(n)^;
        if (box == null) [] else [t.bounds(parent, box.lo, box.hi)^]
    } else {
        let matrix = if (n.matrix != null or n.position != null or n.rotation != null or n.quaternion != null or n.scale != null)
            t.multiply(parent, node_matrix(n)^)^ else parent
        let box = if (contains(DRAWABLES, kind) and n.geometry != null) defs[n.geometry] else null;
        if (box != null) [t.bounds(matrix, box.lo, box.hi)^]
        else [for (child in children(n)) *walk(child, matrix, defs)^]
    }
}

pub fn bounds(asset) any^ {
    let identity = node_matrix({})^
    let boxes = walk(asset, identity, map(geometry_index(asset)^))^;
    if (len(boxes) == 0) {min: [-1.0, -1.0, -1.0], max: [1.0, 1.0, 1.0]}
    else {min: [for (a in 0 to 2) min([for (b in boxes) b.lo[a]])^],
          max: [for (a in 0 to 2) max([for (b in boxes) b.hi[a]])^]}
}

fn has_kind(n, kind) bool^ => name(n) == kind or any([for (child in children(n)) has_kind(child, kind)^])

fn ground_grid(box, center, radius) element {
    let span = radius * 1.5
    let floor_y = box.min[1] - radius * 0.003
    let positions = [for (i in -10 to 10, let p = span*i/10.0)
        *[-span, 0.0, p, span, 0.0, p, p, 0.0, -span, p, 0.0, span]];
    <group id:"model-grid",visible:false,position:[center[0], floor_y, center[2]],
        <'line-segments' width:1.0, 'size-mode':'screen',
            <geometry type:'buffer',positions:positions>
            <material type:'basic',color:"#aab7c9">>>
}

fn toggle(id, caption, pressed = false) element =>
    <button type:"button",id:id,'data-model-action':id,'aria-pressed':string(pressed),caption>
fn select_row(id, caption, choices, initial) element =>
    <div class:"model-field",<label 'for':id,caption>
        <select id:id,'data-model-select':id,
            for (choice in choices) <option value:choice[0],*:(if (choice[0] == initial) {selected:""} else {}),choice[1]>>>
fn slider(id, caption, value, low, high, step) element =>
    <div class:"model-slider",
        <div class:"model-field",<label 'for':id,caption> <output id:(id ++ "-value"),'for':id,string(value)>>
        <input id:id,type:"range",'data-model-range':id,min:low,max:high,step:step,value:value>>
fn section(caption, rows) element =>
    <details open:"open",<summary caption> <div class:"model-fields",*rows>>

fn inspector(clips, points, lights, skeleton) element =>
    <aside id:"model-inspector",'aria-label':"Model settings",
        section("Display", [
            <div class:"model-toggles",
                toggle("model-turn", "Auto-rotate"); toggle("model-wireframe", "Wireframe");
                toggle("model-show-grid", "Grid"); (if (skeleton) toggle("model-skeleton", "Skeleton"));
                toggle("model-screen-pan", "Screen-space pan", true)>,
            select_row("model-background", "Background", BACKGROUNDS, HOME_BACKGROUND),
            *(if (points) [slider("model-point-size", "Point size", 1, 1, 16, 1)] else [])
        ]);
        section("Lighting", [
            select_row("model-environment", "Environment", [["model-studio","Studio"],["model-daylight","Daylight"],["off","Off"]], "model-studio"),
            select_row("model-tone", "Tone mapping", [["none","Linear"],["neutral","Neutral"],["aces","ACES"]], "neutral"),
            slider("model-exposure", "Exposure (EV)", 0, -4, 4, 0.1),
            *(if (lights) [toggle("model-punctual", "Model lights", true)] else []),
            slider("model-ambient-level", "Ambient intensity", 0.3, 0, 3, 0.1),
            select_row("model-ambient-color", "Ambient color", LIGHT_COLORS, "#ffffff"),
            slider("model-key-level", "Direct intensity", 1.5, 0, 5, 0.1),
            select_row("model-key-color", "Direct color", LIGHT_COLORS, "#ffffff")
        ]);
        *(if (len(clips) == 0) [] else [section("Animation", [
            slider("model-speed", "Playback speed", 1, 0.25, 2, 0.25),
            <div class:"model-toggles",
                <button type:"button",id:"model-play",'data-model-action':"model-play","Play">
                if (len(clips) > 1) <button type:"button",id:"model-play-all",'data-model-action':"model-play-all","Play all">>,
            <p id:"model-animation-status",role:"status">,
            <div class:"model-clips",role:"group",'aria-label':"Animation clips",
                for (i,clip in clips) <label class:"model-clip",
                    <input type:"checkbox",'data-model-clip':clip.id,*:(if (i == 0) {checked:""} else {})>
                    <span clip.label or ("Animation " ++ string(i + 1))>>>])])>

pn clip_selection(owner) {
    return [for (box in dom.query_selector_all(owner, "[data-model-clip]") where dom.get_state(box, "checked"))
        dom.get_attribute(box, "data-model-clip")]
}

pn set_playback(owner, selected, start, restart) {
    let scene = scene_dom.node(owner, "model-scene")
    if (start) {
        let animation = runtime.animation_snapshot(scene)^
        // submit the selection as one batch so changes share a tick and playing clips retain their phase.
        let commands = [for (clip in animation.clips, let enabled = restart or contains(selected, clip.clip))
            {op: if (enabled) "play" else "stop", clip:clip.clip, identity:clip.identity, reset:restart}]
        runtime.animation_commands(scene, {session:animation.session,generation:animation.generation,
            service_generation:animation.service_generation,world_identity:animation.world_identity,revision:animation.revision,
            sequence:(int(animation.command_sequence)^ + 1),tick:animation.next_command_tick,commands:commands})^
        runtime.play(scene)^
    } else { runtime.pause(scene)^ }
    scene_dom.text(owner, "model-play", if (start) "Pause" else "Play")
    scene_dom.text(owner, "model-animation-status", "")
}

pn playback(owner, selected, start, restart = false) bool^ {
    let result = set_playback(owner, selected, start, restart)
    if (result is error) {
        scene_dom.text(owner, "model-animation-status", "Playback unavailable")^
        return false
    }
    return true
}

view <model_viewer> state turning:false, frame:null, last_frame:null, playing:false { ~.tree }
on click(evt) {
    let button = dom.closest(evt.target, "[data-model-action]")
    if (button == null) { return 'pass' }
    let owner = dom.closest(button, ".model-document")
    let action = dom.get_attribute(button, "data-model-action")
    let pressed = dom.get_attribute(button, "aria-pressed") != "true"
    let scene = scene_dom.node(owner, "model-scene")
    if (action == "model-play" or action == "model-play-all") {
        let restart = action == "model-play-all"
        let selected = if (playing or restart) [] else clip_selection(owner)
        if (not playing and not restart and len(selected)^ == 0) {
            scene_dom.text(owner, "model-animation-status", "Select an animation to play.")^
            return 'handled'
        }
        let start = restart or not playing
        if (playback(owner, selected, start, restart)^) {
            playing = start
            if (restart) {
                for (box in dom.query_selector_all(owner, "[data-model-clip]")) dom.set_state(box, "checked", true)
            }
        }
        return 'handled'
    }
    if (action == "model-turn") {
        turning = pressed
        if (turning) { frame = dom.request_frame(owner, "model_orbit") }
        else { dom.cancel_frame(owner, frame); frame = null; last_frame = null }
    } else if (action == "model-wireframe") { scene_dom.attr(scene, "wireframe", pressed)^ }
    else if (action == "model-skeleton") { scene_dom.attr(scene, "show-skeleton", pressed)^ }
    else if (action == "model-screen-pan") { viewer.set_screen_pan(owner, "model-camera", pressed)^ }
    else if (action == "model-show-grid") { scene_dom.attr(scene_dom.node(scene, "model-grid"), "visible", pressed)^ }
    else if (action == "model-punctual") {
        for (light in dom.query_selector_all(scene, "#model light")) scene_dom.attr(light, "visible", pressed)^
    }
    scene_dom.attr(button, "aria-pressed", string(pressed))^
    'handled'
}

on model_orbit(evt) {
    if (evt.event_phase != 2 or not turning) { return 'pass' }
    let now = evt.time_stamp
    if (last_frame != null) {
        viewer.rotate_camera(evt.target, "model-camera", max(0.0, min(50.0, now-last_frame))*0.00045)^
    }
    last_frame = now
    frame = dom.request_frame(evt.target, "model_orbit")
    'handled'
}

on change(evt) {
    let owner = dom.closest(evt.target, ".model-document")
    if (dom.get_attribute(evt.target, "data-model-clip") != null) {
        if (playing) {
            playback(owner, clip_selection(owner), true)^
            return 'handled'
        }
        scene_dom.text(owner, "model-animation-status", "")^
        return 'handled'
    }
    let field = dom.get_attribute(evt.target, "data-model-select")
    if (field == null) { return 'pass' }
    let scene = scene_dom.node(owner, "model-scene")
    let value = dom.get_state(evt.target, "value")
    if (field == "model-environment" and value == "off") {
        dom.remove_attribute(scene, "environment")
        return 'handled'
    }
    let setting = {
        'model-background': ["model-scene", "background"], 'model-tone': ["model-scene", "tone-mapping"],
        'model-environment': ["model-scene", "environment"],
        'model-ambient-color': ["model-ambient", "color"], 'model-key-color': ["model-key", "color"]
    }[field]
    scene_dom.attr(scene_dom.node(owner, setting[0]), setting[1], value)^
    'handled'
}

on input(evt) {
    let field = dom.get_attribute(evt.target, "data-model-range")
    if (field == null) { return 'pass' }
    let owner = dom.closest(evt.target, ".model-document")
    let scene = scene_dom.node(owner, "model-scene")
    let value = dom.range_value(evt.target)
    if (field == "model-speed") {
        let result = runtime.time_scale(scene, value)
        if (result is error) { scene_dom.text(owner, "model-animation-status", "Playback speed unavailable")^ }
    } else if (field == "model-exposure") { scene_dom.attr(scene, "exposure", 2.0 ** value)^ }
    else if (field == "model-point-size") {
        for (point in dom.query_selector_all(scene, "#model points")) scene_dom.attr(point, "size", value)^
    } else {
        let id = if (field == "model-ambient-level") "model-ambient" else "model-key"
        scene_dom.attr(scene_dom.node(scene, id), "intensity", value)^
    }
    scene_dom.text(owner, field ++ "-value", string(round(value*100.0)/100.0))^
    'handled'
}

let CSS = "
  html,body { margin:0; height:100%; background:#fbfcfe; color:#212936; font:13px system-ui,sans-serif; }
  .model-document { display:flex; flex-direction:column; height:100vh; }
  .model-header { display:flex; align-items:center; flex:0 0 50px; padding:0 20px; gap:14px;
                  background:#fbfcfe; border-bottom:1px solid #e2e6ec; }
  .model-header h1 { font-size:16px; font-weight:650; min-width:0; margin:0;
                     overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .model-format { flex:none; color:#52657e; font:12px 'SF Mono',Menlo,monospace; }
  .model-workspace { flex:1; min-height:0; display:flex; }
  .model-stage { display:flex; flex-direction:column; flex:1; min-width:0; background:#f0f3f7; }
  .model-stage .three-dimensional { display:flex; flex-direction:column; flex:1; min-height:0; }
  #model-scene { display:block; flex:1; min-height:0; width:100%; touch-action:none; cursor:grab; }
  #model-scene:active { cursor:grabbing; }
  .camera-toolbar { display:flex; flex-wrap:wrap; justify-content:center; gap:4px; padding:8px 10px;
                    border-top:1px solid #dce2eb; background:#fbfcfe; }
  .model-document button { min-height:28px; padding:4px 9px; border:1px solid transparent; border-radius:5px;
                           background:transparent; color:#425570; font:600 12px system-ui,sans-serif; cursor:pointer; }
  .model-document button:hover { background:#e8edf5; color:#31425a; }
  .model-document button[aria-pressed='true'] { background:#dce9fd; color:#1c5da6; }
  .model-document :focus-visible { outline:2px solid #4082ca; outline-offset:-2px; }
  .orbit-help { margin:0; padding:1px 8px 9px; background:#fbfcfe; color:#52657e; font-size:11px; text-align:center; }
  #model-inspector { flex:0 0 276px; min-height:0; overflow:auto; border-left:1px solid #e2e6ec; background:#fff; }
  #model-inspector summary { padding:11px 14px; border-bottom:1px solid #e8edf3; background:#f7f9fc;
                             color:#263448; font-weight:650; cursor:pointer; }
  .model-fields { display:flex; flex-direction:column; gap:9px; padding:10px 14px 13px; }
  .model-field { display:flex; align-items:center; justify-content:space-between; gap:10px; min-height:26px; }
  .model-field label { color:#425570; }
  .model-field select { box-sizing:border-box; width:112px; min-height:27px; padding:3px 6px; border:1px solid #dce2eb;
                        border-radius:5px; background:#fbfcfe; color:#263448; font:12px system-ui,sans-serif; }
  .model-toggles { display:flex; flex-wrap:wrap; gap:4px; }
  .model-toggles button, .model-fields > button { border-color:#dce2eb; }
  .model-fields > button { align-self:flex-start; }
  .model-slider input { display:block; width:100%; margin:0; accent-color:#3878bf; }
  .model-slider output { color:#1c5da6; font:12px 'SF Mono',Menlo,monospace; }
  .model-clip { display:flex; align-items:center; gap:8px; min-height:28px; padding:2px 4px;
                border-bottom:1px solid #e8edf3; color:#263448; cursor:pointer; }
  .model-clip:hover { background:#edf4ff; }
  .model-clip input { flex:none; margin:0; accent-color:#3878bf; }
  .model-clip span { min-width:0; overflow-wrap:anywhere; }
  #model-animation-status { margin:0; color:#8c332c; }
  #model-animation-status:empty { display:none; }
  @media (max-width:620px) {
    .model-workspace { display:block; overflow:auto; }
    .model-stage { height:62vh; min-height:280px; }
    #model-inspector { border-left:0; border-top:1px solid #e2e6ec; overflow:visible; }
  }
"

pub fn render_document_file(source, options = null) element^ {
    let asset = asset_io.load(source, {id:"model"})^
    let box = bounds(asset)^
    let center = [for (axis in 0 to 2) box.min[axis] + (box.max[axis]-box.min[axis])*0.5]
    let extent = camera.subtract(box.max, box.min)
    let radius = max(0.000001, 0.5 * sqrt(camera.dot(extent, extent)))
    let position = camera.add(center, camera.times(camera.unit([0.68, 0.42, 1.0]), radius*3.5))
    let lens = camera.prepare({position:position,target:center,near:radius*0.001,far:radius*160.0},800,600)^
    let clips = [for (root in children(asset)) for (child in children(root)^ where name(child)^ == 'animation-clip') child]
    let points = has_kind(asset, 'points')^
    let lights = has_kind(asset, 'light')^
    let skeleton = has_kind(asset, 'skeleton')^
    let filename = paths.basename(source)^
    let scene = <scene3d id:"model-scene",camera:"model-camera",width:800,height:600,tabindex:"0",'aria-label':"3D model",
        background:HOME_BACKGROUND,'tone-mapping':"neutral",environment:"model-studio",
        <camera id:"model-camera",type:'perspective',position:position,target:center,fov:lens.fov,near:lens.near,far:lens.far>
        <light id:"model-ambient",type:'ambient',color:"#ffffff",intensity:0.3>
        <light id:"model-key",type:'directional',color:"#ffffff",intensity:1.5,position:[4.0,8.0,6.0]>
        environment.room({id:"model-studio"})^
        environment.sky({id:"model-daylight"})^
        ground_grid(box,center,radius)
        asset>
    let stage = viewer.interactive(scene,lens,null,{modes:true,frame_radius:radius,min_distance:radius*0.08,max_distance:radius*40.0,
        reset:"Fit model",help:"Drag to orbit · Shift-drag to pan · Scroll to zoom · R to fit"})^
    let shell = <div class:"model-document",
        <header class:"model-header",<h1 title:filename,filename> <span class:"model-format","GLB">>
        <main class:"model-workspace",<div class:"model-stage",stage>; inspector(clips,points,lights,skeleton)>>;
    <html <head <meta charset:"UTF-8"><title filename><style CSS>>
        <body apply(<model_viewer tree:shell>)>>
}
