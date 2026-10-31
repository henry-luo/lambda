// The gear's parts, modelled in the gear-down pose. Each rigid body is a list of {id, material, mesh};
// the scene wraps a moving body in a group that its keyframe tracks drive. Port parts are the
// starboard ones reflected, so a body is written once.
import m: .mod_mesh
import rig: .mod_rig

let X = [1.0, 0.0, 0.0]
let Y = [0.0, 1.0, 0.0]
let Z = [0.0, 0.0, 1.0]
let ROD_Y = 0.075          // the actuator's centreline above the plate
let PITCH = rig.PI * rig.MODULE

fn piece(id, material, mesh) => {id: id, material: material, mesh: mesh}
fn along(k) => rig.add(rig.JOINT, rig.scale(rig.AXIS, k))
// a starboard body, or its port reflection with "-r" ids renamed "-l"
pub fn sided(pieces, port) => if (not port) pieces else
    [for (q in pieces) {id: replace(q.id, "-r", "-l"), material: q.material, mesh: m.mirrored(q.mesh)}]

// ---- the airframe: the traced plate and what is fixed to it ----

// the plate outline in (x, z), starboard half: a spine, a forward wing and a tailplane, all convex
let PLATE = [
    [[-0.95, 0.0], [0.95, 0.0], [0.95, 0.12], [0.80, 0.20], [-0.80, 0.20], [-0.95, 0.12]],
    [[0.50, 0.20], [0.82, 0.20], [0.74, 0.62], [0.61, 0.62]],
    [[-0.95, 0.12], [-0.80, 0.20], [-0.68, 0.20], [-0.74, 0.40], [-0.88, 0.40]]]
// plate pieces lie in the (x, z) plane; the extrusion runs down from the top face at y = 0
let FLAT = {u: X, v: Z, w: [0.0, -1.0, 0.0]}
fn plate_half(port) => m.merge([for (poly in PLATE)
    m.extrude([for (pt in poly) [pt[0], if (port) 0.0 - pt[1] else pt[1]]], FLAT, 0.0, 0.025)])
fn rivets(z) => m.merge([for (k in 0 to 9) m.cylinder([-0.28 + k * 0.1, 0.045, z], [-0.28 + k * 0.1, 0.045, z + (if (z > 0) 0.004 else -0.004)], 0.007, 10)])

pub fn airframe() => [
    piece("plate-near", "m-plate", plate_half(false)),
    piece("plate-far", "m-plate", plate_half(true)),
    // the channel the actuator lies in, and the slot the rod-end connector runs in
    piece("rails", "m-cream", m.merge([m.cuboid([0.17, 0.045, 0.092], [0.5, 0.045, 0.007]), m.cuboid([0.17, 0.045, -0.092], [0.5, 0.045, 0.007])])),
    piece("rivets", "m-dark", m.merge([rivets(0.099), rivets(-0.099)])),
    piece("slot", "m-slot", m.merge([m.cuboid([-0.14, 0.001, 0.0], [0.16, 0.0015, 0.014]), m.cuboid([-0.14, -0.026, 0.0], [0.16, 0.0015, 0.014])])),
    // pillow blocks carry the output shaft either side of the pinion
    piece("blocks", "m-cream", m.merge([m.cuboid([0.0, -0.11, 0.12], [0.045, 0.085, 0.026]), m.cuboid([0.0, -0.11, -0.12], [0.045, 0.085, 0.026])])),
    *bearing(false), *bearing(true)]
// a collar holds each side tube near its outer end; a strut ties the collar to the plate
fn bearing(port) {
    let collar = along(0.13)
    let top = [collar[0], -0.026, 0.165]
    let strut = m.frame(rig.sub(top, collar))
    let length = sqrt(rig.dot(rig.sub(top, collar), rig.sub(top, collar)));
    sided([piece("collar-r", "m-cream", m.merge([
        m.cylinder(along(0.105), along(0.155), 0.034, 24),
        m.cuboid(rig.add(collar, rig.scale(strut.w, length / 2.0)), [0.013, 0.02, length / 2.0], strut)]))], port)
}

// ---- the hydraulic actuator: a fixed barrel, and the rod that strokes ----

pub fn barrel() => [
    piece("barrel", "m-barrel", m.cylinder([0.10, ROD_Y, 0.0], [0.56, ROD_Y, 0.0], 0.052, 40)),
    // the section view opens the barrel's starboard half to show the piston
    piece("barrel-cut", "m-barrel", m.shell([0.10, ROD_Y, 0.0], [0.56, ROD_Y, 0.0], 0.052, 0.046, -1.5707963, 1.5707963)),
    piece("barrel-caps", "m-dark", m.merge([m.cylinder([0.084, ROD_Y, 0.0], [0.104, ROD_Y, 0.0], 0.057, 40),
        m.cylinder([0.556, ROD_Y, 0.0], [0.576, ROD_Y, 0.0], 0.057, 40)])),
    piece("trunnion", "m-orange", m.merge([m.cuboid([0.61, 0.042, 0.0], [0.034, 0.042, 0.06]),
        m.cylinder([0.17, 0.12, 0.0], [0.17, 0.142, 0.0], 0.012, 12), m.cylinder([0.49, 0.12, 0.0], [0.49, 0.142, 0.0], 0.012, 12)]))]
pub fn rod() => [
    piece("rod-shaft", "m-chrome", m.cylinder([-0.03, ROD_Y, 0.0], [0.32, ROD_Y, 0.0], 0.017, 24)),
    piece("piston", "m-dark", m.cylinder([0.31, ROD_Y, 0.0], [0.335, ROD_Y, 0.0], 0.045, 32)),
    // the rod-end connector drops through the plate slot to drive the rack
    piece("connector", "m-orange", m.merge([m.cuboid([-0.055, ROD_Y, 0.0], [0.03, 0.03, 0.03]),
        m.cuboid([-0.055, -0.004, 0.0], [0.016, 0.066, 0.011])]))]

// ---- the rack, the pinion and the output shaft ----

let RACK_Y = rig.SHAFT[1] + rig.PINION   // the rack's pitch line touches the pinion's pitch circle
pub fn rack() => [piece("rack-bar", "m-steel", m.merge([
    m.cuboid([0.068, RACK_Y + 0.0205, 0.0], [0.235, 0.0155, 0.0175]),
    // a gap sits over the pinion's top tooth at gear down, so the two mesh through the stroke
    *[for (j in -13 to 23, let x = (j + 0.5) * PITCH) m.extrude([[x - 0.4 * PITCH, RACK_Y + 0.005], [x + 0.4 * PITCH, RACK_Y + 0.005],
        [x + 0.18 * PITCH, RACK_Y - rig.MODULE], [x - 0.18 * PITCH, RACK_Y - rig.MODULE]], m.WORLD, -0.0175, 0.0175)]]))]
fn shaft_yoke(port) => sided([piece("yoke-r", "m-orange", m.merge([
    m.cylinder([0.0, rig.SHAFT[1], rig.JOINT_Z - 0.068], [0.0, rig.SHAFT[1], rig.JOINT_Z - 0.04], 0.03, 24),
    m.cuboid([0.0, rig.SHAFT[1] + 0.023, rig.JOINT_Z - 0.03], [0.012, 0.006, 0.016]),
    m.cuboid([0.0, rig.SHAFT[1] - 0.023, rig.JOINT_Z - 0.03], [0.012, 0.006, 0.016])]))], port)
pub fn pinion() => [
    piece("pinion-gear", "m-brass", m.merge([m.gear(rig.SHAFT, m.frame(Z), rig.TEETH, rig.PINION - 1.25 * rig.MODULE, rig.PINION + rig.MODULE, 0.02, rig.PI / 2.0),
        m.cylinder(rig.add(rig.SHAFT, [0.0, 0.0, -0.034]), rig.add(rig.SHAFT, [0.0, 0.0, 0.034]), 0.026, 24)])),
    piece("shaft", "m-steel", m.cylinder(rig.add(rig.SHAFT, [0.0, 0.0, -0.215]), rig.add(rig.SHAFT, [0.0, 0.0, 0.215]), 0.016, 20)),
    *shaft_yoke(false), *shaft_yoke(true)]

// ---- one side: the spinning side tube, its spring leg and the wheel ----

// the tube-side fork of a U-joint; the shaft-side fork's prongs stand at right angles to it
fn fork(f, sign) => m.cuboid(rig.add(along(0.02), rig.scale(f.u, 0.023 * sign)), [0.006, 0.012, 0.016], f)
pub fn side(port) {
    let f = m.frame(rig.AXIS);
    sided([
        piece("spider-r", "m-dark", m.cuboid(rig.JOINT, [0.013, 0.013, 0.013], f)),
        piece("clevis-r", "m-orange", m.merge([m.cylinder(along(0.032), along(0.062), 0.03, 24), fork(f, 1.0), fork(f, -1.0),
            m.cylinder(rig.sub(rig.ROOT, rig.scale(rig.AXIS, 0.06)), rig.add(rig.ROOT, rig.scale(rig.AXIS, 0.035)), 0.036, 24)])),
        piece("tube-r", "m-chrome", m.cylinder(along(0.06), rig.sub(rig.ROOT, rig.scale(rig.AXIS, 0.055)), 0.022, 24)),
        // a painted index line shows the tube turning about its own axis
        piece("index-r", "m-dark", m.cuboid(rig.add(along(0.13), rig.scale(f.v, 0.0215)), [0.004, 0.002, 0.06], f))], port)
}
pub fn leg(port) => sided([
    piece("spring-r", "m-orange", m.sweep(rig.ROOT, rig.add(rig.ROOT, rig.scale(rig.AXIS, 0.11)), rig.add(rig.AXLE_END, [0.0, 0.2, 0.0]),
        rig.AXLE_END, 0.042, 0.02, [0.03, 0.017])),
    piece("boss-r", "m-orange", m.cylinder(rig.sub(rig.AXLE_END, [0.0, 0.0, 0.022]), rig.add(rig.AXLE_END, [0.0, 0.0, 0.02]), 0.03, 24)),
    piece("axle-r", "m-steel", m.cylinder(rig.AXLE_END, rig.add(rig.WHEEL, [0.0, 0.0, 0.03]), 0.016, 16))], port)
pub fn wheel(port) {
    let w = rig.WHEEL
    let f = m.frame(Z)
    let face = rig.add(w, [0.0, 0.0, 0.05]);
    sided([
        piece("tyre-r", "m-rubber", m.torus(w, Z, 0.145, 0.065, 0.062)),
        piece("hub-r", "m-hub", m.cylinder(rig.sub(w, [0.0, 0.0, 0.05]), face, 0.098, 40)),
        // the bolts, cap, valve and brake disc are what show the wheel turning
        piece("bolts-r", "m-dark", m.merge([m.cylinder(face, rig.add(face, [0.0, 0.0, 0.012]), 0.032, 24),
            *[for (k in 0 to 4, let a = 2.0 * rig.PI * k / 5.0) (let o = rig.add(face, rig.add(rig.scale(f.u, 0.06 * cos(a)), rig.scale(f.v, 0.06 * sin(a)))),
                m.cylinder(o, rig.add(o, [0.0, 0.0, 0.01]), 0.008, 10))],
            m.cylinder(rig.sub(w, [0.0, 0.0, 0.07]), rig.sub(w, [0.0, 0.0, 0.058]), 0.085, 40)])),
        piece("valve-r", "m-orange", (let o = rig.add(face, rig.scale(f.u, 0.083)), m.cylinder(o, rig.add(o, [0.0, 0.0, 0.016]), 0.006, 10)))], port)
}

// ---- the runway: a long strip with a dashed centreline that streams aft ----

pub fn ground() => [
    piece("field", "m-ground", m.quad(-30.0, 30.0, -30.0, 30.0)),
    piece("runway", "m-runway", m.quad(-30.0, 90.0, -2.2, 2.2, 0.002)),
    piece("edges", "m-mark", m.merge([m.quad(-30.0, 90.0, 1.95, 2.05, 0.004), m.quad(-30.0, 90.0, -2.05, -1.95, 0.004)]))]
pub fn dashes() => [piece("dash", "m-mark", m.merge([for (k in 0 to 26) m.quad(-9.0 + 3.0 * k, -7.4 + 3.0 * k, -0.09, 0.09, 0.004)]))]
