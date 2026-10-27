// The standard-material fixture. One plain description drives both the native
// scene and the Three.js oracle page (test/scene3d/oracle/), so both renderers
// draw the same vertices. The M1 cases are a 5 x 5 grid of spheres, metalness
// rising to the right and roughness upward; `maps` shows one texture map per
// sphere (textures in assets/pbr/, written by its _generate.py). `hemisphere` adds
// a hemisphere light to the grid, and `room` lights it only by the room
// environment, which the oracle builds with Three.js's own RoomEnvironment.
import s: lambda.scene3d
import shape: lambda.scene3d.shape

let SUN = {type: "directional", color: "#ffffff", intensity: 1.0, position: [3.0, 4.0, 5.0]}
let LIGHTS = {
    direct: [SUN],
    point: [{*:SUN, intensity: 0.4}, {type: "point", color: "#ffd9a8", intensity: 6.0, position: [-2.0, 1.5, 3.0]}]
}
// each case: its lights, ambient light, tone-mapping curve and exposure, and optionally
// a hemisphere light and the environment that lights it
pub let CASES = {
    direct: {lights: LIGHTS.direct, ambient: 0.0, tone: "none", exposure: 1.0},
    point: {lights: LIGHTS.point, ambient: 0.05, tone: "none", exposure: 1.0},
    neutral: {lights: LIGHTS.direct, ambient: 0.05, tone: "neutral", exposure: 1.6},
    aces: {lights: LIGHTS.point, ambient: 0.05, tone: "aces", exposure: 0.9},
    maps: {lights: LIGHTS.direct, ambient: 0.35, tone: "neutral", exposure: 1.0},
    hemisphere: {lights: LIGHTS.direct, ambient: 0.0, hemisphere: {sky: "#bcd7ff", ground: "#6b5a48", intensity: 0.8},
        tone: "none", exposure: 1.0},
    room: {lights: [], ambient: 0.0, environment: "room", tone: "neutral", exposure: 1.0}
}
let GOLD = "#d8a35e"
// the textures of `maps`, by name: a file under test/scene3d/ and whether it holds color
pub let TEXTURES = {checker: {file: "assets/pbr/checker.png", srgb: true}, metal_rough: {file: "assets/pbr/metal_rough.png", srgb: false},
    normal: {file: "assets/pbr/normal.png", srgb: false}, occlusion: {file: "assets/pbr/occlusion.png", srgb: false},
    emissive: {file: "assets/pbr/emissive.png", srgb: true}, leaf: {file: "assets/pbr/leaf.png", srgb: true}}

fn grid() => [for (i in 0 to 4, j in 0 to 4) {position: [(j - 2) * 1.05, (i - 2) * 1.05, 0.0], geometry: "ball",
    material: {color: GOLD, metalness: j / 4.0, roughness: i / 4.0}}]
// one map per sphere, three to a row, each scaled up to show its detail
fn showcase() => (let at = (k) => [(k % 3 - 1) * 1.7, (0.5 - floor(k / 3)) * 1.7, 0.0], [
    {position: at(0), geometry: "ball", material: {color: "#ffffff", roughness: 0.55, map: "checker"}},
    {position: at(1), geometry: "ball", material: {color: "#e8e2da", metalness: 1.0, roughness: 1.0, metal_rough: "metal_rough"}},
    {position: at(2), geometry: "ball", material: {color: GOLD, roughness: 0.4, normal: "normal", normal_scale: 1.0}},
    {position: at(3), geometry: "ball_t", material: {color: GOLD, roughness: 0.4, normal: "normal", normal_scale: 1.0}},
    {position: at(4), geometry: "ball", material: {color: "#c9c4bd", roughness: 0.8, occlusion: "occlusion", occlusion_strength: 1.0,
        emissive: "#ffffff", emissive_intensity: 0.8, emissive_map: "emissive"}},
    {position: at(5), geometry: "ball", material: {color: "#ffffff", roughness: 0.7, map: "leaf", mask: 0.5, double: true}}])

pub fn description(key) {
    let c = CASES[key];
    let textured = key == "maps";
    {key: key, width: 560, height: if (textured) 380 else 560, background: "#202428",
     camera: {position: [0.0, 0.0, if (textured) 9.0 else 10.5], target: [0.0, 0.0, 0.0], fov: 32.0},
     ambient: c.ambient, lights: c.lights, tone: c.tone, exposure: c.exposure,
     hemisphere: c.hemisphere, environment: c.environment,
     geometries: {ball: shape.sphere(16, 32), ball_t: shape.sphere(16, 32, true)},
     textures: if (textured) TEXTURES else {},
     scale: if (textured) 1.5 else 1.0,
     spheres: if (textured) showcase() else grid()}
}

fn material(m) => s.material('standard', {color: m.color, metalness: m.metalness or 0.0, roughness: m.roughness or 1.0,
    *:(if (m.map != null) {texture: m.map} else {}), *:(if (m.metal_rough != null) {'metal-rough-texture': m.metal_rough} else {}),
    *:(if (m.normal != null) {'normal-texture': m.normal, 'normal-scale': m.normal_scale} else {}),
    *:(if (m.occlusion != null) {'occlusion-texture': m.occlusion, 'occlusion-strength': m.occlusion_strength} else {}),
    *:(if (m.emissive_map != null) {emissive: m.emissive, 'emissive-intensity': m.emissive_intensity, 'emissive-texture': m.emissive_map} else {}),
    *:(if (m.mask != null) {'alpha-mode': 'mask', 'alpha-cutoff': m.mask} else {}),
    *:(if (m.double == true) {side: 'double'} else {})})

pub fn scene(d) => s.scene([
    s.camera({id: "camera", position: d.camera.position, target: d.camera.target, fov: d.camera.fov}),
    s.light('ambient', {intensity: d.ambient}),
    *(if (d.hemisphere != null) [s.light('hemisphere', {color: d.hemisphere.sky, 'ground-color': d.hemisphere.ground,
        intensity: d.hemisphere.intensity, position: [0.0, 1.0, 0.0]})] else []),
    *(if (d.environment != null) [s.environment(symbol(d.environment))^] else []),
    *[for (l in d.lights) if (l.type == "point") s.light('point', {color: l.color, intensity: l.intensity, position: l.position})
        else s.light('directional', {color: l.color, intensity: l.intensity, position: l.position, target: [0.0, 0.0, 0.0]})],
    s.resources([
        *[for (name, g in d.geometries) s.geometry(g.positions, {id: string(name), normals: g.normals, uvs: g.uvs, indices: g.indices,
            *:(if (g.tangents != null) {tangents: g.tangents} else {})})],
        *[for (name, t in d.textures) s.texture(t.file, {id: string(name)})]]),
    *[for (b in d.spheres) <mesh geometry: b.geometry, position: b.position, scale: [d.scale, d.scale, d.scale], material(b.material)>]
], {id: "spheres", camera: "camera", width: d.width, height: d.height, background: d.background,
    'tone-mapping': symbol(d.tone), exposure: d.exposure, *:(if (d.environment != null) {environment: d.environment} else {})})
