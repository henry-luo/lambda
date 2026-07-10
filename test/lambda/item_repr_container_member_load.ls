// S0 Item representation MIR fixture: a typed map member access must pass the
// container Item through unchanged, without pointer mask/reconstruction.
type Point = {x: int, y: int}
let p: Point = {x: 3, y: 4}
p.x + p.y
