// A pop-up is a paper illustration on the inside left; its three planes share one flat composition.
import A: .mod_art

pub let DESIGNS = [
    {key: "none", name: "No pop-up", hint: "A traditional flat card.", color: "#e11d48", label: "", layers: [[], [], []]},
    {key: "cake", name: "Birthday surprise", hint: "A cake, balloons and a birthday banner.", color: "#f472b6", label: "MAKE A WISH!", layers: [
        [{art: "star", x: 64, y: 64, w: 42, color: "#fbbf24"}, {art: "balloons", x: 266, y: 58, w: 80, color: "#fb7185"}],
        [{art: "cake", x: 92, y: 126, w: 216, color: null}],
        [{art: "confetti", x: 14, y: 270, w: 100, color: "#fbbf24"}, {art: "confetti", x: 288, y: 272, w: 100, color: "#38bdf8"}]]},
    {key: "hearts", name: "Hearts in bloom", hint: "Layered hearts with flowers at their feet.", color: "#e11d48", label: "LOVE YOU MORE", layers: [
        [{art: "sparkle", x: 60, y: 80, w: 46, color: "#d4a017"}, {art: "sparkle", x: 296, y: 92, w: 40, color: "#d4a017"}],
        [{art: "hearts", x: 96, y: 100, w: 208, color: null}],
        [{art: "flower", x: 32, y: 268, w: 106, color: "#fb7185"}, {art: "flower", x: 272, y: 276, w: 96, color: "#f9a8d4"}]]},
    {key: "garden", name: "Secret garden", hint: "Flowers and a butterfly in a paper garden.", color: "#a78bfa", label: "HAPPINESS BLOOMS", layers: [
        [{art: "sun", x: 260, y: 48, w: 70, color: "#fbbf24"}, {art: "butterfly", x: 62, y: 74, w: 84, color: null}],
        [{art: "flower", x: 72, y: 164, w: 128, color: null}, {art: "tulip", x: 218, y: 132, w: 96, color: "#fb923c"}],
        [{art: "sprig", x: 32, y: 246, w: 82, color: "#4d7c0f"}, {art: "flower", x: 270, y: 274, w: 102, color: "#f9a8d4"}]]},
    {key: "winter", name: "Winter wonderland", hint: "An evergreen rising through snowy paper banks.", color: "#2f6b3a", label: "MERRY & BRIGHT", layers: [
        [{art: "snowflake", x: 66, y: 62, w: 54, color: "#38bdf8"}, {art: "snowflake", x: 282, y: 90, w: 46, color: "#93c5fd"}],
        [{art: "tree", x: 116, y: 92, w: 168, color: null}],
        [{art: "tree", x: 24, y: 256, w: 78, color: "#6b8f71"}, {art: "snowflake", x: 282, y: 270, w: 74, color: "#38bdf8"}]]}]
pub let DEPTHS = [{key: "low", name: "Low", scale: .65}, {key: "classic", name: "Classic", scale: 1.0}, {key: "high", name: "High", scale: 1.4}]
pub let CAMERA = {perspective: 2400.0, pitch: 6.0, yaw: -14.0, half_yaw: -24.0}
let PLANE_DEPTHS = [.04, .13, .22]
pub fn design(key) => (let hit = [for (p in DESIGNS where p.key == key) p], if (len(hit) == 0) DESIGNS[0] else hit[0])
pub fn depth(key) => (let hit = [for (p in DEPTHS where p.key == key) p], if (len(hit) == 0) DEPTHS[1] else hit[0])
fn valid_color(c: string) => len(c) == 7 and starts_with(c, "#")
    and all([for (ch in split(lower(slice(c, 1)), "")) contains("0123456789abcdef", ch)])
pub fn of(card) => (let p = design(card.popup.design),
    {design: p.key, color: if (card.popup.color is string) (if (valid_color(card.popup.color)) card.popup.color else p.color) else p.color,
        depth: depth(card.popup.depth).key})
pub fn configure(card, field, value) {
    let before = of(card)
    let next = if (field == "design") {*:before, design: design(value).key, color: design(value).color}
        else {*:before, [field]: value};
    {*:card, popup: of({popup: next})}
}

pub fn camera_style() => "--popup-pitch:" ++ A.n(CAMERA.pitch) ++ "deg;--popup-yaw:" ++ A.n(CAMERA.yaw)
    ++ "deg;--popup-half-yaw:" ++ A.n(CAMERA.half_yaw) ++ "deg"
pub fn fold_pose(progress) => {lift: progress, opacity: min(1.0, progress * 3.0),
    angle: if (progress <= .5) -88.0 + progress * 80.0 else -48.0 + (progress - .5) * 72.0}
fn rad(deg) => deg * math.pi / 180.0
fn rotated(x, y, z, w, h, pose, cover) {
    let angle = if (cover) rad(pose.fold) else 0.0
    let cx = x * math.cos(angle) + z * math.sin(angle) - w / 2.0
    let cz = 0.0 - x * math.sin(angle) + z * math.cos(angle)
    let cy = y - h / 2.0
    let pitch = rad(pose.pitch)
    let py = cy * math.cos(pitch) - cz * math.sin(pitch)
    let pz = cy * math.sin(pitch) + cz * math.cos(pitch)
    let yaw = rad(pose.yaw);
    {x: cx * math.cos(yaw) + pz * math.sin(yaw) + pose.offset * w,
     y: py, z: 0.0 - cx * math.sin(yaw) + pz * math.cos(yaw)}
}
fn fit_axis(value, z, available) => (let half = available / 2.0,
    let denom = abs(value) + half * z / CAMERA.perspective,
    if (denom <= 0.0) 1.2 else half / denom)
pub fn preview_scale(card, w, h, available_w, available_h) {
    let popup = of(card).design != "none"
    let lift = if (popup) min(w, h) * max(PLANE_DEPTHS) * depth(of(card).depth).scale else 0.0
    let poses = [for (i in 0 to 12) (let p = float(i) / 12.0,
        {fold: -180.0 * p, offset: .5 * p, pitch: if (popup) CAMERA.pitch else 0.0,
         yaw: if (popup) CAMERA.yaw + (CAMERA.half_yaw - CAMERA.yaw) * math.sin(math.pi * p) else 0.0})];
    // Fit the whole turn, including perspective between the resting poses. Perspective gives
    // s <= available/2 / (abs(axis) + available/2 * z / camera_distance).
    max(.01, min(1.2, min([for (pose in poses) for (cover in [false, true])
        for (x in [0.0, float(w)]) for (y in [0.0, float(h)])
        for (z in (if (cover and pose.fold != 0.0) [0.0, 0.0 - lift] else [0.0]))
            (let p = rotated(x, y, z, w, h, pose, cover),
             min(fit_axis(p.x, p.z, available_w), fit_axis(p.y, p.z, available_h)))])))
}

fn placed_art(it, ink) {
    let a = A.art_of(it.art);
    <svg x:it.x, y:it.y, width:it.w, height:it.w * A.ratio(it.art), viewBox:"0 0 " ++ string(a.box[0]) ++ " " ++ string(a.box[1]),
        (a.draw)(if (it.color == null) ink else it.color)>
}
fn plane_svg(p, config, i) => <svg class:"popup-art", viewBox:"0 0 400 440", width:"100%", height:"100%", 'aria-hidden':"true",
    if (i == 0) <g
        <path d:"M28 388V176C28-12 372-12 372 176V388Z", fill:A.tint(config.color, .88), stroke:A.tint(config.color, .5), 'stroke-width':3>
        <path d:"M46 380V178C46 6 354 6 354 178V380", fill:"none", stroke:"#ffffff", 'stroke-width':3>>;
    for (it in p.layers[i]) placed_art(it, config.color);
    if (i == 2) <g
        <path d:"M22 342Q200 312 378 342L368 386Q200 358 32 386Z", fill:config.color>
        <text x:200, y:357, fill:if (A.is_dark(config.color)) "#ffffff" else "#3f3a33", 'text-anchor':"middle",
            'font-family':"Helvetica, Arial, sans-serif", 'font-size':16, 'font-weight':700, 'letter-spacing':1, p.label>>>

pub fn scene(card, w, h, live = false) {
    let config = of(card)
    let p = design(config.design)
    let lift = depth(config.depth).scale;
    if (p.key == "none") null
    else <div class:"popup-scene " ++ (if (live) "popup-live" else "popup-flat"), role:"img", 'aria-label':p.name ++ " pop-up",
        'data-popup':p.key, 'data-depth':config.depth, style:"perspective:" ++ A.n(min(w, h) * 3) ++ "px",
        <div class:"popup-ground", style:"background:" ++ A.tint(config.color, .8)>;
        for (i in 0 to 2) (let z = A.n(min(w, h) * PLANE_DEPTHS[i] * lift),
            <div class:"popup-plane popup-plane-" ++ string(i), 'data-plane':string(i), 'data-lift':z,
            style:"--popup-z:" ++ z ++ "px",
            if (i > 0) <span class:"popup-tab", style:"background:" ++ A.tint(config.color, .65)>;
            (plane_svg(p, config, i))>)>
}
