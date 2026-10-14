import s: lambda.scene3d

fn sphere_point(latitude, longitude, rows, columns) array {
    let theta = math.pi * latitude / rows
    let phi = 2.0 * math.pi * longitude / columns;
    [sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)]
}

// the duplicated seam keeps triangles local in longitude; normals remain unit length.
pub fn sphere(id, rows, columns, colors = null) element {
    let positions = [for (j in 0 to rows) for (i in 0 to columns)
        *sphere_point(j, i, rows, columns)]
    let indices = [for (j in 0 to (rows - 1), i in 0 to (columns - 1),
        let a = j * (columns + 1) + i, let b = a + columns + 1)
        *[a, a + 1, b, a + 1, b + 1, b]];
    s.geometry(positions, {id: id, normals: positions, indices: indices,
        *: (if (colors != null) {colors: colors} else {})})
}
