// S16.9.5: `a?: T` marks an optional FIELD — the whole field may be absent.
// It parses in every field position the ruling names. It used to be accepted
// only on parameters; a map-type field or an element attribute was rejected
// with `error[E103]: invalid type pattern`, which also made the validator's own
// fixture (test/validator_test_data/maps.ls) fail against itself.
type MapOpt = {name: string, opt?: int}
type MapMulti = {a?: int, b?: string, c: bool}
type ElmtOpt = <e a?: string>
type ElmtMixed = <e a?: string, b: int>
type Nested = {outer: {inner?: int}}

// the sibling spellings must keep working alongside it
type Nullable = {a: int?}
type Plain = {a: int}
fn param_opt(a: int, b?: int) => if (b) a + b else a

// the marker is a type-level annotation; values are unaffected
let m = {name: "x", opt: 1}
let e = <e a: "s", b: 2>

let out = [param_opt(1), param_opt(1, 2), m.name, m.opt, e.a, e.b]
out
