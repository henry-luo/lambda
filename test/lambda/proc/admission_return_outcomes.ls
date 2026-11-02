// S11.4.1v5/S7.5.4: a deferred return check contributes to its call's outcome.
fn opaque(value) => value
fn checked_array(value) float[] => opaque(value)
fn checked_scalar(value) int => opaque(value)
fn checked_bool(value) bool => opaque(value)
fn forwarded(value) float[]^ => checked_array(value)
pn checked_explicit(value) float[] { return opaque(value) }
pn main() {
    let inferred = checked_array("bad")
    let admitted: float[]^ = checked_array("bad")
    var changed: float[]^ = []
    changed = checked_array("bad")
    let scalar = checked_scalar("bad")
    let flag = checked_bool("bad")
    let explicit = checked_explicit("bad")
    let forward = forwarded("bad")
    let recovered = checked_array("bad") ^ { 42 }
    let good: float[]^ = checked_array([1.5]);
    [inferred is error, admitted is error, changed is error, scalar is error,
     flag is error, explicit is error, forward is error, recovered, good[0], "continued"]
}
