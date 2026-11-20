// S12.1.1v5 and D9.5.6v2: sample the static plan as a value, then upload only the visible map to Radiant.
import img: lambda.image
import resources: ~~.mod_resources
import land: .mod_land
import u: .mod_util

pub let SIZE = 200
pub let CENTER = SIZE / 2.0
pub let REGION = {x0: land.X0, x1: land.X1, z0: land.Z0, z1: land.Z1}
let WIDTH = REGION.x1 - REGION.x0
let DEPTH = REGION.z1 - REGION.z0
// fit the corners inside the circular clip, with room around the terrain.
let FIT = (SIZE - 16.0) / sqrt(WIDTH * WIDTH + DEPTH * DEPTH)
pub let SCALES = [FIT, 0.10, 0.15, 0.22, 0.31, 0.44, 0.62, 0.88, 1.24, 1.76, 2.48]
pub let DEFAULT_ZOOM = 6
pub let OVERVIEW = {x: REGION.x0 + WIDTH / 2.0, z: REGION.z0 + DEPTH / 2.0, turn: 0.0}

pub fn view_pose(car, zoom, pan) any^ {
    let center = pan or {x: car.x, z: car.z, turn: car.heading - u.PI}
    let scale = SCALES[zoom];
    {*: center, zoom: zoom, scale: scale, c: scale * cos(center.turn), s: scale * sin(center.turn)}
}

pub fn projected(pose, x, z) any^ {
    let dx = x - pose.x, dz = z - pose.z;
    [pose.c * dx - pose.s * dz, pose.s * dx + pose.c * dz]
}

pub fn pan(pose, dx, dy) any^ {
    let squared_scale = pose.scale * pose.scale;
    {x: u.clamp(pose.x - (pose.c * dx + pose.s * dy) / squared_scale, REGION.x0, REGION.x1),
     z: u.clamp(pose.z - (pose.c * dy - pose.s * dx) / squared_scale, REGION.z0, REGION.z1), turn: pose.turn}
}

pub pn load_plan() {
    let uri = url_resolve(resources.entry_uri()^, "assets/minimap.png")
    let bytes = input(uri, 'binary')^
    let pixels = load(bytes)^
    let raster = img.from_array(pixels)^
    return raster
}

pub fn frame(plan, pose) any^ {
    let offset = projected(pose, REGION.x0, REGION.z0)
    let dx = WIDTH / plan.width, dz = DEPTH / plan.height
    // map source pixels into the viewport before the HUD clips the disc.
    let layer = img.layer(plan, {transform: [pose.c * dx, pose.s * dx, 0.0 - pose.s * dz, pose.c * dz,
        CENTER + offset[0], CENTER + offset[1]]});
    img.render_frame(layer, {width: SIZE, height: SIZE})^.pixels
}
