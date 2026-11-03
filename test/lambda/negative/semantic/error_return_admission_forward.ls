// S11.4.1v5: the call's validation error is outside its successful-value target.
fn opaque(value) => value
fn checked(value) float[] => opaque(value)
fn forward() float[] => checked("bad")
forward()
