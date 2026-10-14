import data: ~~.mod_data
import model: ~~.mod_model
import math

fn steps(st,count,factor) => if (count == 0) st else steps(model.zoom(st,factor),count-1,factor)
fn span(st) => math.norm(model.camera(st))*math.tan(model.fov(st)*math.pi/360.0)

let initial = model.initial()
let immune = model.select_cell(initial,"whiteBlood")
let saved = model.favorite(immune)
let limited = model.orbit(model.zoom(initial,100.0),0.5,100.0)
let enlarged = steps(initial,24,0.85)
let restored = steps(enlarged,24,1.0/0.85)
{
    specimens:len(data.cells),
    defaults:all([for (c in data.cells) data.organelle(c.id,c.defaultOrganelle) != null]),
    comparisons:all([for (c in data.cells) data.cell(c.comparison) != null and c.comparison != c.id]),
    selected:immune.cell == "whiteBlood" and immune.part == data.cell("whiteBlood").defaultOrganelle,
    removed:all([for (id in ["neuron","bacteria","animal"]) data.cell(id) == null and model.select_cell(initial,id) == initial]),
    invalid_selection:model.select_cell(initial,"missing") == initial,
    save:contains(saved.favorites,"whiteBlood"),
    unsave:len(model.favorite(saved).favorites) == 0,
    saved_filter:model.matches({*:saved,library:true},data.cell("whiteBlood")) and
        not model.matches({*:saved,library:true},data.cell("plant")),
    camera_limits:limited.distance == 18.0 and limited.pitch == 1.3,
    near_limit:model.zoom(initial,0.001).distance == 0.05,
    deep_zoom:model.fov(enlarged) < 2.0 and all([for (i in 1 to 30)
        span(steps(initial,i,0.85)) < span(steps(initial,i-1,0.85))]),
    camera_clearance:abs(math.norm(model.camera(enlarged))-4.0) < 0.000001,
    projection_scale:abs(4.0*math.tan(model.fov(enlarged)*math.pi/360.0)-
        enlarged.distance*math.tan(model.fov(initial)*math.pi/360.0)) < 0.000001,
    zoom_reversible:abs(restored.distance-initial.distance) < 0.000001 and model.fov(restored) == model.fov(initial),
    reset:model.reset(limited).yaw == initial.yaw and model.reset(limited).distance == initial.distance and
        model.fov(model.reset(enlarged)) == model.fov(initial)
}
