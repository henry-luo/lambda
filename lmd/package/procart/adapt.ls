// Adapters (Lambda_Pkg_Proc_Art.md PG4): build the data other packages already
// accept. Pure constructors only; rendering stays in those packages.
import img: lambda.image
import core: ._field

// a colormap is [[t, [r, g, b]], ...] with t ascending in [0, 1]; channels are 0..255.
pub fn gray() array => [[0.0, [0, 0, 0]], [1.0, [255, 255, 255]]]
pub fn terrain() array => [[0.0, [20, 40, 110]], [0.45, [50, 110, 190]], [0.5, [220, 210, 150]],
    [0.6, [80, 150, 70]], [0.8, [110, 100, 80]], [1.0, [250, 250, 250]]]

fn channel(v: float) int {
    let c: float = max(0.0, min(255.0, v));
    int(floor(c + 0.5))
}

fn color_at(stops, t: float) array {
    let v = max(0.0, min(1.0, t))
    let above = [for (i, s in stops where s[0] >= v) i]
    let hi = if (len(above) == 0) len(stops) - 1 else max(1, above[0])
    let a = stops[hi - 1]
    let b = stops[hi]
    let span = b[0] - a[0]
    let u = if (span <= 0.0) 1.0 else min(1.0, max(0.0, (v - a[0]) / span));
    [for (c in 0 to 2) channel(float(a[1][c]) + u * float(b[1][c] - a[1][c]))]
}

// RGBA u8 samples, shaped [height, width, 4], from a field in [0, 1].
pub fn pixels(field, colormap = null) array {
    let stops = if (colormap == null) gray() else colormap
    let h = len(field)
    let w = if (h == 0) 0 else len(field[0]);
    reshape([for (row in field) for (v in row) for (c in [*color_at(stops, float(v)), 255]) u8(c)], [h, w, 4])
}

pub fn image(field, colormap = null) element^ => img.from_array(pixels(field, colormap))^

// lambda.chart rectilinear grid data with the field as one vertex field.
pub fn chart_grid(field, label = "value") map {
    core.grid_data(field, label)
}

// buffer-geometry arrays accepted by lambda.scene3d.geometry; +Y is up.
pub fn heightfield(field, options = {}) map^ {
    let h = len(field)
    let w = if (h == 0) 0 else len(field[0])
    let sx = if (options.dx == null) 1.0 else options.dx
    let sz = if (options.dz == null) 1.0 else options.dz
    let scale = if (options.height_scale == null) 1.0 else options.height_scale;
    if (not core.valid(field) or w < 2 or h < 2 or not core.finite(sx) or not core.finite(sz) or not core.finite(scale) or sx <= 0 or sz <= 0)
        raise error("procart.adapt.heightfield: expected a finite field and positive grid spacing")
    else {positions: [for (y, row in field) for (x, v in row) for (p in [float(x) * sx, float(v) * scale, float(y) * sz]) p],
        indices: [for (y in 0 to (h - 2)) for (x in 0 to (w - 2), let a = y * w + x)
            for (i in [a, a + w, a + 1, a + 1, a + w, a + w + 1]) i],
        uvs: [for (y in 0 to (h - 1)) for (x in 0 to (w - 1)) for (v in [float(x) / float(w - 1), float(y) / float(h - 1)]) v]}
}

// pure SVG construction; callers choose its placement and rendering (PG4).
pub fn svg(lines, options = {}) element^ {
    if (not (lines is array) or not all([for (line in lines) line is array and
        all([for (p in line) p is array and len(p) >= 2 and core.finite(p[0]) and core.finite(p[1])])]))
        raise error("procart.adapt.svg: expected arrays of finite [x,y] points")
    else <svg xmlns: "http://www.w3.org/2000/svg", viewBox: options.view_box,
        width: options.width, height: options.height,
        *[for (line in lines) <polyline fill: "none", stroke: if (options.stroke == null) "currentColor" else options.stroke,
            'stroke-width': if (options.stroke_width == null) 1.0 else options.stroke_width,
            points: join([for (p in line) string(p[0]) ++ "," ++ string(p[1])], " ")>]>
}
