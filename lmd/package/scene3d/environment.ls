// Procedural environments: ordinary scene content inside an <environment>, which Radiant
// captures from the environment's origin and prefilters for image-based light
// (S12.1.1v2: Lambda builds the scene data, the host owns the capture and the filter).
import t: .transform
import shape: .shape

// Three.js r186 RoomEnvironment: a white box room lit by one point light and six emissive
// panels, with six white boxes standing in it. Each box: position, rotation about y, scale.
let ROOM_BOXES = [
    [[-10.906, 2.009, 1.846], -0.195, [2.328, 7.905, 4.651]], [[-5.607, -0.754, -0.758], 0.994, [1.970, 1.534, 3.955]],
    [[6.167, 0.857, 7.803], 0.561, [3.927, 6.285, 3.687]], [[-2.017, 0.018, 6.124], 0.333, [2.002, 4.566, 2.064]],
    [[2.291, -0.756, -2.621], -0.286, [1.546, 1.552, 1.496]], [[-2.193, -0.369, -5.547], 0.516, [3.875, 3.487, 2.986]]]
// each panel: emissive intensity, position, scale
let ROOM_PANELS = [
    [50.0, [-16.116, 14.37, 8.208], [0.1, 2.428, 2.739]], [50.0, [-16.109, 18.021, -8.207], [0.1, 2.425, 2.751]],
    [17.0, [14.904, 12.198, -1.832], [0.15, 4.265, 6.331]], [43.0, [-0.462, 8.89, 14.520], [4.38, 5.441, 0.088]],
    [20.0, [3.235, 11.486, -12.541], [2.5, 2.0, 0.1]], [100.0, [0.0, 20.0, 0.0], [1.0, 0.1, 1.0]]]

pub fn room(options = {}) element^ {
    let id = string(options.id or "room")
    let box = id ++ "-box"
    let instances = [for (b in ROOM_BOXES) t.matrix(b[0], [0.0, b[1], 0.0], b[2])^]
    // Three.js's 900 is a physical intensity; native units carry its π (SP1)
    let lamp = <light type: 'point', color: "#ffffff", intensity: 900.0 / math.pi, distance: 28.0, position: [0.418, 16.199, 0.300]>
    let walls = <mesh geometry: box, position: [-0.757, 13.219, 0.717], scale: [31.713, 28.305, 28.591],
        <material type: 'standard', color: "#ffffff", side: 'back'>>
    let boxes = <mesh geometry: box, instances: instances, <material type: 'standard', color: "#ffffff">>
    let panels = [for (p in ROOM_PANELS) <mesh geometry: box, position: p[1], scale: p[2],
        <material type: 'lambert', color: "#000000", emissive: "#ffffff", 'emissive-intensity': p[0]>>]
    let shared = <resources <geometry id: box, type: 'box'>>
    let floor_offset = [0.0, -3.5, 0.0]
    let contents = <group position: floor_offset, *[lamp, walls, boxes, *panels]>;
    <environment id: id, *: options, *[shared, contents]>
}

fn mix3(a, b, f) array => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * f]
let SKY_KEYS = ["zenith", "horizon", "ground", "sun-direction", "sun-size", "sun-intensity"]
// A sky dome around the capture point: sRGB colors (0..1 triplets) blending from the horizon
// up to the zenith, a flat ground color below, and a sun disc of `sun-size` radians angular
// radius glowing at `sun-intensity` along `sun-direction`.
pub fn sky(options = {}) element^ {
    let id = string(options.id or "sky")
    let zenith = options.zenith or [0.24, 0.45, 0.75], horizon = options.horizon or [0.78, 0.86, 0.92]
    let ground = options.ground or [0.36, 0.33, 0.30]
    let toward = options["sun-direction"] or [0.4, 0.8, 0.45]
    let length = math.sqrt(sum([for (x in toward) x * x]))
    let checked = if (not (length > 0.0)) raise error("scene3d: sun direction must not be zero")
    let size = options["sun-size"] or 0.04, glow = options["sun-intensity"] or 40.0
    let radius = 50.0
    let dome = shape.sphere(24, 48)
    let colors = [for (i in 0 to len(dome.normals) / 3 - 1, let y = dome.normals[i * 3 + 1])
        *(if (y < 0.0) ground else mix3(horizon, zenith, math.sqrt(y)))]
    let sun = shape.sphere(8, 16)
    let distance = radius * 0.9, diameter = 2.0 * distance * math.tan(size)
    let rest = map([for (k, v in options where not contains(SKY_KEYS, string(k))) (string(k), v)])
    let sky_dome = <mesh scale: [radius * 2.0, radius * 2.0, radius * 2.0],
        <geometry type: 'buffer', positions: dome.positions, normals: dome.normals, colors: colors, indices: dome.indices>
        <material type: 'basic', color: "#ffffff", side: 'back'>>
    let sun_disc = <mesh position: [for (x in toward) x / length * distance], scale: [diameter, diameter, diameter],
        <geometry type: 'buffer', positions: sun.positions, normals: sun.normals, indices: sun.indices>
        <material type: 'basic', color: "#000000", emissive: "#ffffff", 'emissive-intensity': glow>>;
    <environment id: id, far: radius * 2.0, *: rest, *[sky_dome, sun_disc]>
}
