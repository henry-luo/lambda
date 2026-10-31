// The native scene: materials, light, the runway, and every part under its explode wrapper. The gear
// moves through one native clip sampled from mod_rig, so playback, seeking and speed stay on the
// scene clock; display modes are one-off DOM edits handled by skymaster.ls.
import s: lambda.scene3d
import m: .mod_mesh
import rig: .mod_rig
import model: .mod_model

pub let BACKGROUND = "#eadcc4"
pub let HOLD = 1.0          // the clip outlasts its sequence so the end pose holds until the page pauses it
// exploded view: each assembly lifts or drops away from the plate along its own direction
pub let EXPLODE = {'x-plate': [0.0, 0.16, 0.0], 'x-actuator': [0.0, 0.36, 0.0], 'x-rack': [0.0, 0.05, 0.0],
    'x-gearbox': [0.0, -0.14, 0.0], 'x-right': [0.0, -0.12, 0.26], 'x-left': [0.0, -0.12, -0.26]}

fn material(id, color, options = {}) element => s.material('standard', {id: id, color: color, roughness: 0.55, *: options})
pub fn plate_material(xray) => {opacity: if (xray) 0.4 else 1.0}
fn materials(display) => [
    material("m-plate", "#efe3cd", {roughness: 0.8, side: 'double', *: plate_material(display.xray)}),
    material("m-cream", "#f1e7d6", {roughness: 0.6}),
    material("m-barrel", "#f6f1e8", {roughness: 0.35, side: 'double'}),
    material("m-orange", "#e4532c", {roughness: 0.42}),
    material("m-chrome", "#e3e5e9", {metalness: 1.0, roughness: 0.18}),
    material("m-brass", "#cf9c3a", {metalness: 0.9, roughness: 0.3}),
    material("m-steel", "#3a3b40", {metalness: 0.7, roughness: 0.38}),
    material("m-dark", "#26262a", {metalness: 0.3, roughness: 0.5}),
    material("m-rubber", "#1f1e1d", {roughness: 0.92}),
    material("m-hub", "#f4f2ee", {metalness: 0.15, roughness: 0.35}),
    material("m-slot", "#4a4038", {roughness: 0.9}),
    material("m-ground", "#e6d7bd", {roughness: 1.0}),
    material("m-runway", "#d9c6a5", {roughness: 1.0}),
    material("m-mark", "#f7f0e2", {roughness: 0.9}),
    material("m-fuselage", "#f6ecda", {opacity: 0.2, side: 'double', roughness: 0.4})]

fn meshes(pieces, hidden = []) => [for (q in pieces)
    s.mesh(m.geometry("g-" ++ q.id, q.mesh), q.material, {id: q.id, *: (if (contains(hidden, q.id)) {visible: false} else {})})]
// a moving body: a group its tracks drive, holding parts modelled in world coordinates
fn body(id, pieces, hidden = []) element => s.group(meshes(pieces, hidden), {id: id})
fn wrapper(id, children, display) element => s.group(children, {id: id, position: if (display.exploded) EXPLODE[id] else [0.0, 0.0, 0.0]})

// the sequence clip; replacing it (a new id) restarts the gear at the sequence's first pose
pub fn clip(seq) element {
    let times = rig.sample_times(seq);
    s.clip("seq-" ++ seq.name, [for (tr in rig.tracks(seq)) s.track(tr.path, times, tr.values, tr.kind)],
        {duration: seq.duration + HOLD, autoplay: true})
}
// the plate's near half and the closed barrel give way to the cut-away in section view
pub fn section_hidden(section) => if (section) ["plate-near", "barrel"] else ["barrel-cut"]

pub fn scene(seq, display, camera) element {
    let hidden = section_hidden(display.section);
    s.scene([
        s.camera({id: "cam", fov: camera.fov, near: 0.05, far: 80.0, position: camera.position, target: camera.target}),
        s.light('hemisphere', {color: "#fff6e6", 'ground-color': "#b89f7c", intensity: 0.32}),
        s.light('directional', {position: [2.4, 6.0, 3.6], target: [0.0, -0.5, 0.0], color: "#fff0d8", intensity: 0.85,
            'cast-shadow': true, 'shadow-map-size': 2048, 'shadow-extent': 3.4, 'shadow-normal-bias': 0.02, 'shadow-softness': 2.5}),
        <fog type: 'linear', color: BACKGROUND, near: 6.0, far: 18.0>,
        s.resources([s.environment('room')^, *materials(display)]),
        s.group([*meshes(model.ground()), body("dashes", model.dashes())], {id: "ground"}),
        wrapper("x-plate", meshes(model.airframe(), hidden), display),
        wrapper("x-actuator", [*meshes(model.barrel(), hidden), body("rod", model.rod())], display),
        wrapper("x-rack", [body("rack", model.rack())], display),
        wrapper("x-gearbox", [body("pinion", model.pinion())], display),
        wrapper("x-right", [body("side-r", model.side(false)), body("leg-r", model.leg(false)), body("wheel-r", model.wheel(false))], display),
        wrapper("x-left", [body("side-l", model.side(true)), body("leg-l", model.leg(true)), body("wheel-l", model.wheel(true))], display),
        s.mesh(s.sphere(24, 48, {id: "g-fuselage"}), "m-fuselage", {id: "fuselage", position: [-0.05, 0.28, 0.0],
            scale: [2.5, 0.84, 1.32], visible: display.fuselage, 'cast-shadow': false}),
        clip(seq)
    ], {id: "gear-scene", camera: "cam", width: 1280, height: 720, viewBox: "0 0 1280 720", preserveAspectRatio: "xMidYMid slice",
        background: BACKGROUND, environment: "room", 'environment-intensity': 0.3, 'tone-mapping': 'none', exposure: 1.0,
        tabindex: "0", role: "img", 'aria-label': "Cessna 337 main landing gear retraction mechanism, interactive 3D model"})
}
