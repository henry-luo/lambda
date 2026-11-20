// S12.1.1v5 and D9.5.6v2: sample the static plan as a value, then upload only the visible map to Radiant.
import img: lambda.image
import resources: ~~.mod_resources

pub let SIZE = 200
pub let CENTER = SIZE / 2.0
pub let SCALE = 0.62
pub let REGION = {x0: -320.0, x1: 1620.0, z0: -460.0, z1: 460.0}

pub pn load_plan() {
    let uri = url_resolve(resources.entry_uri()^, "assets/minimap.png")
    let bytes = input(uri, 'binary')^
    let pixels = load(bytes)^
    let raster = img.from_array(pixels)^
    return raster
}

pub fn frame(plan, car, turn) any^ {
    let c = SCALE * cos(turn), s = SCALE * sin(turn)
    let x = REGION.x0 - car.x, z = REGION.z0 - car.z
    let dx = (REGION.x1 - REGION.x0) / plan.width, dz = (REGION.z1 - REGION.z0) / plan.height
    // map source pixels into the car-centred viewport before the HUD clips the disc.
    let layer = img.layer(plan, {transform: [c * dx, s * dx, 0.0 - s * dz, c * dz,
        CENTER + c * x - s * z, CENTER + s * x + c * z]});
    img.render_frame(layer, {width: SIZE, height: SIZE})^.pixels
}
