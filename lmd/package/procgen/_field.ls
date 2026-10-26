// shared field validation and interchange shape (PG5); no rendering dependencies.
pub fn finite(v) bool => v is number and not (v is complex) and float(v) - float(v) == 0.0
pub fn valid(f, mask = false) bool => f is array and
    len(f) * (if (len(f) == 0) 0 else len(f[0])) <= 1000000 and
    all([for (row in f) row is array and len(row) == len(f[0]) and
        all([for (v in row) finite(v) or (mask and v is bool)])])
pub fn grid_data(f, label = "value") map =>
    {kind: "grid", x: [for (x in 0 to (if (len(f) == 0) -1 else len(f[0]) - 1)) x],
     y: [for (y in 0 to (len(f) - 1)) y], vertex_fields: map([label, f])}
