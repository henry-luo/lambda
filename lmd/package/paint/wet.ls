// Native wet sheets (PT14, PE9): retain the reference's metadata and numeric storage.
import watercolor: lambda.procart.watercolor
import radiant
import media: lambda.paint.media
import strokes: lambda.paint.stroke

// Both supported lane types are stepped in place; reference sheets carry three pigments.
pub fn from_reference(s) map^ => {*:s, pigments:if (s.pigments == null) 3 else s.pigments}
pub fn layout(pigments: int) map^ => radiant.paint_wet_layout(pigments)^
pub fn sheet(width: int, height: int, seed: int, scale: float = 2.5, wetness: float = 0.0, pigments: int = 3) map^ {
    let spec = layout(pigments)^;
    {*:watercolor.sheet_bounded(width, height, seed, scale, wetness, 1000000, spec.planes)^, pigments:pigments}
}

// Composite the sheet over the underlying pigment layer without changing either value (PE8, PE9).
pub fn present(s, layer, pigments, rect = null, encode = 'srgb') array^ =>
    radiant.paint_wet_present(s.cells, layer, {pigments:media.table(pigments)^, rect:rect, encode:encode})^

// Place the same ordered dab rows used by the other media into the wet planes (PT7, PE9).
pub pn stroke(var s, b, color, points, pigment: int, water: float, previous = null, options = {}) map^ {
    let o = strokes.options_checked(options)^
    if (pigment < 0 or pigment >= s.pigments or water < 0.0 or water > 64.0) {
        raise error("paint: wet stroke needs a pigment index and water in range")
    }
    if (strokes.washes(b)^) {
        raise error("paint: wet wash strokes are not supported")
    }
    if (previous != null and (previous.brush != b or previous.color != color or previous.pigment != pigment or previous.water != water)) {
        raise error("paint: wet stroke continuation does not match its brush and medium")
    }
    let placed = if (o.paper == null) ({*:b, grain:0.0}) else b
    let d = strokes.dabs(placed, color, points, if (previous == null) null else previous.carry, 'linear',
        {pigment:float(pigment), amount:b.amount, water:water, thickness:0.0})^
    let active = if (s.x1 < s.x0 or s.y1 < s.y0) null else [s.x0, s.y0, s.x1 + 1, s.y1 + 1]
    let spent = if (previous == null) 0 else previous.work
    let bristly = string(b.kind)^ == "bristle"
    let bristle_start = if (not bristly) null else if (previous == null) strokes.bristle_state(b, points[0], [b.amount, water, 0.0])^
        else previous.bristles
    let result = if (d.count == 0) ({active:active, rect:null, work:0}) else
        if (bristly) radiant.paint_wet_bristles(s.cells, d.table, bristle_start, {width:s.width, height:s.height, pigments:s.pigments,
            active:active, since_shrink:s.since_shrink, wet_cells:s.wet_cells, budget:strokes.BUDGET - spent,
            paper:o.paper, channel:pigment, depletion:b.depletion, pickup:b.pickup})^
        else radiant.paint_wet_dabs(s.cells, d.table, {width:s.width, height:s.height, pigments:s.pigments, active:active,
            since_shrink:s.since_shrink, wet_cells:s.wet_cells, budget:strokes.BUDGET - spent, tip:b.tip, paper:o.paper})^
    if (result.active != null) {
        s.x0 = result.active[0]
        s.y0 = result.active[1]
        s.x1 = result.active[2] - 1
        s.y1 = result.active[3] - 1
    }
    return {brush:b, color:color, pigment:pigment, water:water, carry:d.carry,
        bristles:if (d.count == 0) bristle_start else result.state, rect:result.rect,
        bounds:strokes.union(if (previous == null) null else previous.bounds, result.rect), work:spent + result.work}
}

// A batch advances one unshared array in place; metadata follows the engine's activity bounds.
pub pn step(var s, adsorption, settings = watercolor.SETTINGS, steps: int = 1, dry_only: bool = false) map^ {
    let active = if (s.x1 < s.x0 or s.y1 < s.y0) null else [s.x0, s.y0, s.x1 + 1, s.y1 + 1]
    let options = {width:s.width, height:s.height, active:active, since_shrink:s.since_shrink,
        wet_cells:s.wet_cells, steps:steps, wet_diffusion:s.wet_diffusion, pigment_diffusion:s.pigment_diffusion,
        edge_kernel:s.edge_kernel, edge_kernel_large:s.edge_kernel_large, velocity_clamp:s.velocity_clamp,
        tilt:settings.tilt, evaporation:settings.evaporation, edges:settings.edges, drying:settings.drying,
        adsorption:adsorption, dry_only:dry_only}
    let result = radiant.paint_wet_step(s.cells, options)^
    if (result.active == null) {
        s.x0 = 0
        s.x1 = -1
        s.y0 = 0
        s.y1 = -1
    } else {
        s.x0 = result.active[0]
        s.y0 = result.active[1]
        s.x1 = result.active[2] - 1
        s.y1 = result.active[3] - 1
    }
    s.since_shrink = result.since_shrink
    s.wet_cells = result.wet_cells
    if (not dry_only) { s.steps = s.steps + steps }
    return result
}

// Settle the sheet without advecting it (PE9).
pub pn dry(var s, pigments) map^ {
    return step(s, watercolor.adsorption(pigments), {tilt:0.0, evaporation:0.0, edges:false, drying:true}, 1, true)^
}

// Transfer only dry deposits; the engine validates the full region before changing either value.
pub pn deposit(var layer, var s, pigments, rect = null) map^ {
    if (s.width != layer.width or s.height != layer.height) {
        raise error("paint: wet sheet and pigment layer must have the same extent")
    }
    let options = {rect:rect, pigments:media.table(pigments)^}
    return radiant.paint_wet_deposit(layer, s.cells, options)^
}
