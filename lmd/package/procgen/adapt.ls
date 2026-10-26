// Adapters (Lambda_Pkg_Proc_Gen.md PG4): build the data other packages already
// accept. Pure constructors only; rendering stays in those packages.
import img: lambda.image

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
    let h = len(field)
    let w = if (h == 0) 0 else len(field[0]);
    {kind: "grid", x: [for (i in 0 to (w - 1)) i], y: [for (j in 0 to (h - 1)) j], vertex_fields: map([label, field])}
}
