// A value passed as `any` and stored into a map field or an element attribute
// must come back as itself. The any-lane store (`typeditem_store_item`) had no
// case for a range or a sized number, so both read back as an error while the
// same literal written in place, or stored in an array, survived. Found by the
// semantics model (vibe/Lambda_Semantics_DSL_POC.md F21): its environment is
// an element whose `v` attribute holds every bound value.

fn attr(v) => <b v: v>
fn field(v) => {v: v}
fn cell(v) => [v]

let r = 2 to 4;
[type(attr(r).v), type(field(r).v), type(cell(r)[0])];
[attr(r).v, field(r).v];
[type(attr(12u8).v), type(field(12u8).v), type(cell(12u8)[0])];
[attr(12u8).v, field(-3i16).v, attr(2.5f32).v];
// the literal forms, unchanged
[type(<b v: (2 to 4)>.v), type({v: 12u8}.v)]
