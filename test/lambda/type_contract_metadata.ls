// Implicit and explicit any must retain distinct source contracts even though
// both select the same compact Item carrier. The forward value and closure
// exercise the paths that retain the TypeFunc signature beyond a declaration.
let forward_value = forward
fn forward(value) { value }
fn explicit(value: any) any { value }
fn precise() { 1 }
pn procedural(value) { value }
let captured = (value) => value
let converted = int("3");

[forward_value(1), captured(2), explicit(converted)]
