// S7.5.2v4/S11.4.1v5: a known call admission outcome cannot enter a clean array.
fn opaque(value) => value
fn checked(value) float[] => opaque(value)
let value: float[] = checked("bad")
value
