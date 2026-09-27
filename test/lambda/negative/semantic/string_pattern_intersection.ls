// S11.1.2v3 (SP20): `|` is the only binary operator inside a pattern island;
// intersect whole patterns instead, as in `\(a+) & \(w+)`.
type bad_intersection = \(a & w)
