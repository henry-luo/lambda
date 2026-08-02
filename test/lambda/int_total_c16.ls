// C16 total-int gate. Pins the properties the rotation boxing scheme exists to
// provide (vibe/Lambda_Type_Int_Boxing.md, vibe/Lambda_Impl_Int_Total.md §4):
// int arithmetic is total, `int` stays distinguishable from `float` at every
// magnitude, poison is a value rather than an escape, and none of it depends on
// where the value is stored.
fn show(x) => [type(x), x]
fn dv(a, b) => a div b
fn md(a, b) => a % b
fn vdiv(v, d) => v div d

// Totality: arithmetic at and past the band edge stays `int`. Before C16 these
// promoted to float; the values are exact here and the type never changes.
[show(9007199254740991 + 9007199254740991), show(9007199254740991 * 2)]

// The O1 divergence repro. A 32-bit overflow product used to become a float
// when boxed while staying int when consumed natively; now it is int either way.
// The value is the correctly-rounded binary64 answer, which is C16's domain
// answer rather than a loss of precision.
[show(2147483647 * 2147483647)]

// Poison closes the domain instead of leaving it: `div`/`%` by zero yield int's
// own inf/nan, not float's and not error().
[show(dv(7, 0)), show(dv(-7, 0)), show(dv(0, 0)), show(md(7, 0))]

// ... and poison is an ordinary value, so it survives a container.
[[dv(7, 0), dv(-7, 0), dv(0, 0)]]

// Per-lane poison in a packed int array (spec §4.7). This is only expressible
// because the `int` lane is double-backed: an i64 lane has no inf/nan.
[vdiv([6, 0, 8], 0)]

// A value above the 2^53 band round-trips through a list and a map field. The
// retired carrier could not do this — it borrowed frame-scoped storage and
// dangled on escape.
let big = 9007199254740991 * 4
[show(big), [big], {v: big}]

// C16 is "tagged, not erased": int and float stay distinct at every magnitude,
// including out past the band where the value is carried as its own IEEE bits.
[type(1.0), type(1), type(big), type(big + 0.5)]
