// The engine's dab-table row (Radiant_Design_Paint_Engine Appendix A, layout v1, 34 columns).
// Every brush kind builds its rows here, so the column order lives in one place.
pub let COLUMNS = 34
pub let BLENDS = ['normal', 'multiply', 'screen', 'overlay', 'darken', 'lighten', 'add']

// d holds every field: x, y, radius, aspect, angle (radians), hardness, aa, opacity, flow,
// r, g, b, eraser (1 paints), lock_alpha, colorize, posterize, levels, smudge, smudge_length,
// smudge_radius (0: no pickup), bucket, dh, dl, dhsl_s, dv, dhsv_s, tip (-1: procedural),
// grain, profile (0 mypaint, 1 linear) and blend. On a pigment layer (PE8) d also holds pigment
// (a pigment-table row), amount (pigment load), water (wet-sheet load) and thickness (paint body), per unit coverage.
pub fn row(d) array => [d.x, d.y, d.radius, d.aspect, d.angle, d.hardness, d.aa, d.opacity, d.flow,
    d.r, d.g, d.b, d.eraser, d.lock_alpha, d.colorize, d.posterize, d.levels,
    d.smudge, d.smudge_length, d.smudge_radius, d.bucket, d.dh, d.dl, d.dhsl_s, d.dv, d.dhsv_s,
    d.tip, d.grain, if (d.pigment == null) -1.0 else d.pigment, if (d.amount == null) 0.0 else d.amount,
    if (d.water == null) 0.0 else d.water,
    if (d.thickness == null) 0.0 else d.thickness, d.profile, d.blend]

// the column value of a validated blend mode name.
pub fn blend_index(name) float^ => float(sum([for (i, b in BLENDS) if (string(b) == string(name)) i else 0]))^
