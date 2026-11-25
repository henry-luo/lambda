// What the angler does, as pose functions of time in boat space, and the clips sampled from them.
// A pose names the torso, head, arm and forearm turns ([yaw, pitch, roll], radians), the oars
// ([sweep, lift]), the rod (grip position and turn) and the line (how the bobber and hook hang).
// The host starts clips; it reads the same functions for where the bobber, oar blades and a
// hooked fish are, so ripples and the fish land exactly where the animation puts them.
import s: lambda.scene3d
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
import u: ~~.midtown.mod_util
import boat: .mod_boat
import fisher: .mod_fisher

pub let CAST_SPOT = [-0.8, 0.01, 4.4]          // where the bobber lands, ahead and to starboard (-X)
pub let BOATSIDE = [-0.78, -0.05, 1.3]          // where a hooked fish is drawn in
pub let SHOW = [-0.78, 0.95, 0.55]              // where the catch dangles for a look, clear of the rod
pub let RELEASE = [-1.95, -0.12, 0.9]           // where it goes back in
let FPS = 20.0

fn lerp(a: float, b: float, k: float) float => a + (b - a) * k
fn vlerp(a, b, k) => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * k]
fn ease(x: float) float => (let c = max(0.0, min(1.0, x)), c * c * (3.0 - 2.0 * c))
// a quick rise and a springy settle: the bounce that makes motion feel alive
fn spring(x: float) float => if (x <= 0.0) 0.0 else 1.0 - exp(-6.0 * x) * cos(11.0 * x)
fn span(x: float, a: float, b: float) float => max(0.0, min(1.0, (x - a) / (b - a)))
fn bump(x: float, at: float, width: float) float => (let d = (x - at) / width, if (abs(d) >= 1.0) 0.0 else 0.5 + 0.5 * cos(d * math.pi))
fn arc(a, b, k: float, height: float) => (let p = vlerp(a, b, k), [p[0], p[1] + height * 4.0 * k * (1.0 - k), p[2]])
fn mix_angles(a, b, k) => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * k]

// ---- the building blocks
let OARS_REST = {l: [0.0, 0.2], r: [0.0, 0.2]}
let OARS_SHIPPED = {l: [1.38, -0.04], r: [1.38, -0.04]}
let ROD_STOWED = {p: [-0.3, 0.08, -0.9], a: [0.15, -0.08, 0.0]}
let ROD_READY = {p: [-0.25, 0.44, 0.1], a: [-0.22, -0.78, 0.0]}
// hands: "oar" (its handle), "grip" (the rod's grip), "butt" (the rod's end), "knee", or a boat-space point
let ARMS_OARS = {hand_r: "oar", hand_l: "oar"}
let ARMS_ROD = {hand_r: "grip", hand_l: "knee"}
fn base(arms, oars, rod, line) => {torso: [0.0, 0.03, 0.0], scale: 1.0, head: [0.0, 0.08, 0.0], *: arms, oars: oars, rod: rod, line: line}
fn blend(a, b, k) any^ => {torso: mix_angles(a.torso, b.torso, k), scale: lerp(a.scale, b.scale, k), head: mix_angles(a.head, b.head, k),
    hand_r: vlerp(resolve(a, "r")^, resolve(b, "r")^, k), hand_l: vlerp(resolve(a, "l")^, resolve(b, "l")^, k),
    oars: {l: [lerp(a.oars.l[0], b.oars.l[0], k), lerp(a.oars.l[1], b.oars.l[1], k)], r: [lerp(a.oars.r[0], b.oars.r[0], k), lerp(a.oars.r[1], b.oars.r[1], k)]},
    rod: {p: vlerp(a.rod.p, b.rod.p, k), a: mix_angles(a.rod.a, b.rod.a, k)}, line: if (k < 0.5) a.line else b.line}
let STOW = {mode: "stow"}
pub let IDLE_POSE = base(ARMS_OARS, OARS_REST, ROD_STOWED, STOW)
pub let READY_POSE = base(ARMS_ROD, OARS_SHIPPED, ROD_READY, STOW)
fn floating(bob) => {mode: "float", bob: bob}

// ---- each action as a pose at time x seconds into it
// rowing: x is the stroke phase 0..1; push-rowing, the blades sweep aft in the water while the hands push forward
pub fn row(x: float) {
    let f = x - floor(x)
    let sweep = if (f < 0.5) -0.5 + ease(f / 0.5) else 0.5 - ease((f - 0.5) / 0.5)
    // blades in the water through the push, lifted for the return, dropped back in at the end of it
    let wet = if (f < 0.44) 1.0 else if (f < 0.5) 1.0 - span(f, 0.44, 0.5) else span(f, 0.92, 1.0)
    let lift = 0.08 + 0.19 * wet
    let reach = 0.5 + 0.5 * sin(sweep * 3.1);
    {*: IDLE_POSE, torso: [0.0, 0.05 + 0.13 * reach, 0.0], head: [0.0, 0.0 - 0.06 * reach, 0.0],
     oars: {l: [sweep, lift], r: [sweep, lift]}}
}
pub let ROW_PERIOD = 1.15
pub fn idle(x: float) => {*: IDLE_POSE, torso: [0.0, 0.03 + 0.012 * sin(x * 2.1), 0.0], scale: 1.0 + 0.012 * sin(x * 2.1),
    head: [0.22 * sin(x * 0.7), 0.08 + 0.03 * sin(x * 0.9), 0.0]}
pub let IDLE_PERIOD = 2.99
pub fn ready(x: float) => {*: READY_POSE, torso: [0.0, 0.03 + 0.012 * sin(x * 2.1), 0.0], scale: 1.0 + 0.012 * sin(x * 2.1),
    rod: {p: ROD_READY.p, a: [ROD_READY.a[0], ROD_READY.a[1] + 0.02 * sin(x * 2.1), 0.0]}, head: [0.18 * sin(x * 0.7), 0.1, 0.0]}
pub let READY_PERIOD = 2.99
pub fn to_fish(x: float) any^ => (let k = ease(x / 0.8), {*: blend(IDLE_POSE, READY_POSE, k)^, torso: [0.0, 0.03 + 0.1 * bump(x, 0.35, 0.35), 0.0]})
pub fn to_row(x: float) any^ => (let k = ease(x / 0.8), {*: blend(READY_POSE, IDLE_POSE, k)^, torso: [0.0, 0.03 + 0.1 * bump(x, 0.45, 0.35), 0.0]})
// the cast: wind up over the shoulder, whip forward, the bobber flies and bounces on landing
pub fn cast(x: float) any^ {
    let wind = ease(span(x, 0.0, 0.42)), whip = ease(span(x, 0.42, 0.56)), settle = ease(span(x, 0.56, 1.25))
    let pitch = lerp(lerp(lerp(ROD_READY.a[1], -2.05, wind), -0.32, whip), -0.6, settle)
    let rod = {p: [-0.25, 0.44 + 0.2 * wind * (1.0 - whip), 0.1 - 0.12 * wind * (1.0 - whip)], a: [-0.22 + 0.12 * wind * (1.0 - whip), pitch, 0.0]}
    let pose = {*: READY_POSE, torso: [0.0, 0.03 - 0.1 * wind * (1.0 - whip) + 0.14 * whip * (1.0 - settle), 0.0], scale: 1.0 + 0.05 * bump(x, 0.5, 0.15),
        hand_l: if (x < 0.75) "butt" else "knee", rod: rod}
    let launch = rod_tip({p: [-0.25, 0.44, 0.1], a: [-0.15, -0.5, 0.0]})^
    let fly = span(x, 0.5, 1.0)
    let landed = x - 1.0
    let bounce = if (landed > 0.0) 0.06 * exp(-7.0 * landed) * cos(landed * 24.0) else 0.0;
    {*: pose, line: if (x < 0.5) STOW else if (x < 1.0) {mode: "fly", bob: arc(launch, CAST_SPOT, fly, 1.1)}
        else floating([CAST_SPOT[0], CAST_SPOT[1] + bounce, CAST_SPOT[2]])}
}
pub let CAST_LANDS = 1.0
pub let CAST_TIME = 1.25
fn waiting(x: float, rod_pitch: float, bob) => {*: READY_POSE, rod: {p: ROD_READY.p, a: [-0.22, rod_pitch, 0.0]},
    torso: [0.0, 0.03 + 0.012 * sin(x * 2.1), 0.0], head: [0.1 * sin(x * 0.6), 0.14, 0.0], line: floating(bob)}
pub fn wait(x: float) => waiting(x, -0.6 + 0.015 * sin(x * 2.1), [CAST_SPOT[0], CAST_SPOT[1] + 0.012 * sin(x * u.TAU / 1.5), CAST_SPOT[2]])
pub let WAIT_PERIOD = 3.0
pub fn nibble(x: float) => waiting(x, -0.6 - 0.03 * (bump(x, 0.14, 0.08) + bump(x, 0.42, 0.08)),
    [CAST_SPOT[0], CAST_SPOT[1] - 0.04 * bump(x, 0.12, 0.08) - 0.03 * bump(x, 0.4, 0.08), CAST_SPOT[2]])
pub let NIBBLE_TIME = 0.7
pub let NIBBLES = [0.12, 0.4]
pub let BITE_WINDOW = 1.05
pub let BITE_TIME = 1.35
// a bite drags the float under; with no strike the fish lets go and it pops back up
pub fn bite(x: float) {
    let under = ease(span(x, 0.0, 0.16)) * (1.0 - ease(span(x, BITE_WINDOW, BITE_TIME)))
    let tremble = 0.02 * sin(x * 40.0) * under;
    {*: waiting(x, -0.6 + 0.16 * under + tremble, [CAST_SPOT[0] + 0.3 * under, CAST_SPOT[1] - 0.22 * under, CAST_SPOT[2] + 0.12 * under]),
     torso: [0.0, 0.03 + 0.08 * under, 0.0], head: [0.0, 0.22 * under + 0.1, 0.0]}
}
pub fn reel(x: float) any^ {
    let k = ease(span(x, 0.0, 0.7))
    let rod = {p: ROD_READY.p, a: [-0.22, lerp(-0.6, -1.0, bump(x, 0.25, 0.3)) + (ROD_READY.a[1] + 0.6) * k, 0.0]};
    {*: READY_POSE, rod: rod, hand_l: if (k >= 1.0) "knee" else crank(rod, x)^,
     line: if (k >= 1.0) STOW else {mode: "fly", bob: arc(CAST_SPOT, stow_bob(rod)^, k, 0.5)}}
}
pub let REEL_TIME = 0.9

// ---- the strike: yank, a fight to the boatside, the lift, a look at the catch, and its release
pub let FIGHT = 1.8
pub let LIFTED = 2.3
pub let LET_GO = 3.5
pub let BACK_IN = 4.1
pub let STRIKE_TIME = 4.6
// the hook's path, boat space
pub fn hook(x: float) any^ => if (x < FIGHT) (let k = ease(x / FIGHT), let p = vlerp(CAST_SPOT, BOATSIDE, k),
        [p[0] + 0.35 * sin(x * 7.0) * (1.0 - k), -0.1 + 0.07 * sin(x * 11.0), p[2]])
    else if (x < LIFTED) arc([BOATSIDE[0], -0.05, BOATSIDE[2]], SHOW, ease(span(x, FIGHT, LIFTED)), 0.35)
    else [SHOW[0], SHOW[1] + 0.04 * sin(x * 9.0), SHOW[2]]
// the caught fish: hooked by the nose until it is let go, then tossed back in
pub fn catch_pose(x: float, length: float) any^ {
    if (x >= LET_GO) {
        let k = span(x, LET_GO, BACK_IN)
        return {p: arc(SHOW, RELEASE, k, 0.45), yaw: -1.2 - 2.0 * k, pitch: lerp(-0.95, 1.0, k), roll: 1.2 * (1.0 - k)}
    }
    let h = hook(x)^
    let tip = rod_tip(strike(x)^.rod)^
    let yaw = math.atan2(tip[0] - h[0], tip[2] - h[2]) + 0.45 * sin(x * 15.0) * (if (x < FIGHT) 1.0 else 0.4)
    // hung by the lip, a little off vertical and turned side-on to the camera so it reads from above
    let pitch = if (x < FIGHT) 0.0 else lerp(0.0, -0.95, ease(span(x, FIGHT, FIGHT + 0.3)))
    let roll = if (x >= LIFTED) 1.2 + 0.45 * sin(x * 13.0) else 0.0
    let fwd = t.quaternion_rotate(u.quat(yaw, pitch, roll)^, [0.0, 0.0, length / 2.0])^
    return {p: [h[0] - fwd[0], h[1] - fwd[1], h[2] - fwd[2]], yaw: yaw, pitch: pitch, roll: roll}
}
pub fn strike(x: float) any^ {
    let yank = spring(x / 0.35)
    let lift = ease(span(x, FIGHT, LIFTED))
    let cheer = if (x >= LIFTED and x < LET_GO) 1.0 else 0.0
    let toss = bump(x, (LET_GO + BACK_IN) / 2.0, 0.35)
    let settle = ease(span(x, BACK_IN, STRIKE_TIME))
    let h = hook(x)^
    let aim = math.atan2(h[0] - ROD_READY.p[0], h[2] - ROD_READY.p[2])
    let pitch = lerp(lerp(-0.6 - 0.5 * yank + 0.08 * sin(x * 6.0) * (if (x < FIGHT) 1.0 else 0.0), -1.25, lift), ROD_READY.a[1], settle)
    let rod = {p: ROD_READY.p, a: [lerp(lerp(aim, -0.25, lift), ROD_READY.a[0], settle), pitch, 0.0]}
    let squash = 1.0 + 0.08 * bump(x, LIFTED + 0.15, 0.15) - 0.05 * bump(x, LIFTED + 0.4, 0.12) + 0.03 * bump(x, LIFTED + 0.62, 0.12)
    let line = if (x < LET_GO) {mode: "taut", hook: h} else if (x < BACK_IN) {mode: "fly", bob: arc(SHOW, stow_bob(rod)^, span(x, LET_GO, BACK_IN), 0.2)} else STOW;
    {*: READY_POSE, rod: rod, scale: squash,
     torso: [0.0, 0.03 - 0.16 * yank * (1.0 - lift) - 0.08 * lift * (1.0 - settle), 0.0],
     head: [0.25 * cheer * sin(x * 3.0), 0.1 - 0.35 * lift * (1.0 - settle), 0.0],
     // the free hand cranks the reel through the fight, waves at the catch, then tosses it back
     hand_l: if (x < FIGHT) crank(rod, x * 1.4)^ else if (x < LIFTED) "butt" else if (x < LET_GO) [0.3, 1.12 + 0.06 * sin(x * 14.0), -0.06]
        else if (x < BACK_IN) vlerp([-0.55, 0.9, 0.5], [-0.95, 0.7, 0.75], span(x, LET_GO, BACK_IN)) else "knee", line: line}
}

// ---- geometry the host shares
pub fn rod_tip(rod) any^ => (let d = t.quaternion_rotate(u.quat(rod.a[0], rod.a[1], rod.a[2])^, [0.0, 0.0, boat.ROD])^, [rod.p[0] + d[0], rod.p[1] + d[1], rod.p[2] + d[2]])
fn stow_bob(rod) any^ => (let tip = rod_tip(rod)^, [tip[0], tip[1] - 0.2, tip[2]])
// the four line bones: rod tip, mid-line, bobber, hook (boat space)
pub fn line_points(pose) any^ {
    let tip = rod_tip(pose.rod)^
    let l = pose.line
    let bob = if (l.mode == "stow") stow_bob(pose.rod)^ else if (l.mode == "taut") (let d = v.subtract(tip, l.hook), let n = sqrt(v.dot(d, d)),
        vlerp(l.hook, tip, min(0.35 / max(n, 0.001), 0.5))) else l.bob
    let hk = if (l.mode == "taut") l.hook else if (l.mode == "float") [bob[0], bob[1] - 0.32, bob[2]] else [bob[0], bob[1] - 0.06, bob[2]]
    let d = v.subtract(bob, tip)
    let sag = if (l.mode == "float") 0.12 * sqrt(v.dot(d, d)) else if (l.mode == "fly") 0.05 * sqrt(v.dot(d, d)) else 0.02
    let mid = vlerp(tip, bob, 0.55)
    return [tip, [mid[0], max(if (l.mode == "float") 0.03 else -1.0, mid[1] - sag), mid[2]], bob, hk]
}
// where the bobber is (boat space), for ripples and for fish deciding to bite
pub fn bobber(pose) any^ => line_points(pose)^[2]
// oar blade centres (boat space), for the rings each stroke leaves
pub fn blades(pose) any^ => [for (side in [-1.0, 1.0], let tag = if (side < 0.0) "r" else "l", let o = pose.oars[tag],
    let d = t.quaternion_rotate(oar_q(side, o)^, [side * (boat.OAR_OUT - 0.23), 0.0, 0.0])^)
    [side * boat.OARLOCK[0] + d[0], boat.OARLOCK[1] + d[1], boat.OARLOCK[2] + d[2]]]
fn yq(a: float) => [0.0, sin(a / 2.0), 0.0, cos(a / 2.0)]
fn zq(a: float) => [0.0, 0.0, sin(a / 2.0), cos(a / 2.0)]
fn oar_q(side: float, o) any^ => t.quaternion_multiply(yq(side * o[0]), zq(0.0 - side * o[1]))

// ---- where the hands are: a named grip resolved against the pose's oars and rod
pub fn rod_point(rod, offset) any^ => (let d = t.quaternion_rotate(u.quat(rod.a[0], rod.a[1], rod.a[2])^, offset)^, [rod.p[0] + d[0], rod.p[1] + d[1], rod.p[2] + d[2]])
// the reel handle going round, for the free hand
fn crank(rod, x) any^ => rod_point(rod, [0.0 + 0.035 * cos(x * 14.0), -0.075 + 0.035 * sin(x * 14.0), -0.03])^
fn handle(side, o) any^ => (let d = t.quaternion_rotate(oar_q(side, o)^, [0.0 - side * (boat.OAR_IN - 0.07), 0.0, 0.0])^,
    [side * boat.OARLOCK[0] + d[0], boat.OARLOCK[1] + d[1], boat.OARLOCK[2] + d[2]])
pub fn resolve(pose, which) any^ {
    let spec = if (which == "r") pose.hand_r else pose.hand_l
    let side = if (which == "r") -1.0 else 1.0
    if (spec is array) { return spec }
    if (spec == "oar") { return handle(side, pose.oars[which])^ }
    if (spec == "grip") { return pose.rod.p }
    if (spec == "butt") { return rod_point(pose.rod, [0.0, -0.01, -0.16])^ }
    return [side * fisher.KNEE_L[0], fisher.KNEE_L[1], fisher.KNEE_L[2]]
}

// ---- clips: every action sampled at FPS into the same set of tracks
fn q3(a) any^ => u.quat(a[0], a[1], a[2])^
// a one-shot clip holds its last pose for HOLD seconds, so switching to the next clip a frame late never shows a wrap
let HOLD = 1.0
fn sample(duration, loop) any^ => (let n = int(ceil(duration * FPS))^, [*[for (i in 0 to n) min(duration, i / FPS)], *(if (loop) [] else [duration + HOLD])])
fn track(path, times, values, kind) any^ => s.track(path, times, values, kind)^
pub fn clip(id, f, duration, loop) any^ {
    let times = sample(duration, loop)^
    let poses = [for (x in times) f(min(x, duration))^]
    let lines = [for (p in poses) line_points(p)^]
    let hands = [for (p in poses) {r: resolve(p, "r")^, l: resolve(p, "l")^}];
    s.clip(id, [
        *(fisher.tracks(poses, hands, times, track)^),
        *[for (side in [-1.0, 1.0], let tag = if (side < 0.0) "r" else "l")
            track("oar-" ++ tag ++ ".quaternion", times, [for (p in poses, c in oar_q(side, p.oars[tag])^) c], 'quaternion')^],
        track("rod.position", times, [for (p in poses, c in p.rod.p) c], 'vector')^,
        track("rod.quaternion", times, [for (p in poses, c in q3(p.rod.a)^) c], 'quaternion')^,
        *[for (k in 0 to 3) track("ln-" ++ string(k) ++ ".position", times, [for (pts in lines, c in pts[k]) c], 'vector')^]],
        {duration: if (loop) duration else duration + HOLD, autoplay: id == "idle"})
}
pub fn clips() any^ => [clip("idle", idle, IDLE_PERIOD, true)^, clip("row", (x) any^ => row(x / ROW_PERIOD), ROW_PERIOD, true)^,
    clip("ready", ready, READY_PERIOD, true)^,
    clip("to-fish", to_fish, 0.8, false)^, clip("to-row", to_row, 0.8, false)^, clip("cast", cast, CAST_TIME, false)^,
    clip("wait", wait, WAIT_PERIOD, true)^, clip("nibble", nibble, NIBBLE_TIME, false)^, clip("bite", bite, BITE_TIME, false)^,
    clip("reel", reel, REEL_TIME, false)^, clip("strike", strike, STRIKE_TIME, false)^]
pub let CLIP_IDS = ["idle", "row", "ready", "to-fish", "to-row", "cast", "wait", "nibble", "bite", "reel", "strike"]
