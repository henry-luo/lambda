// v5 total-int gate. Finite arithmetic closes at int53 while the shared poison
// values remain ordinary int surface values through every storage route.
fn show(x) => [type(x), x]
fn dv(a, b) => a div b
fn md(a, b) => a % b
fn vdiv(v, d) => v div d

// Totality: finite results past the band become shared signed infinity.
[show(9007199254740991 + 9007199254740991), show(9007199254740991 * 2)]

// A native product follows the same saturation rule as its boxed counterpart.
[show(2147483647 * 2147483647)]

// Poison closes the domain instead of leaving it: `div`/`%` by zero yield int's
// own inf/nan, not float's and not error().
[show(dv(7, 0)), show(dv(-7, 0)), show(dv(0, 0)), show(md(7, 0))]

// ... and poison is an ordinary value, so it survives a container.
[[dv(7, 0), dv(-7, 0), dv(0, 0)]]

// Packed int arrays store private i64 poison sentinels and box them as shared
// IEEE values on an Item read.
[vdiv([6, 0, 8], 0)]

// A value above the int53 band stays saturated through a list and map field.
let big = 9007199254740991 * 4
[show(big), [big], {v: big}]

// Shared poison reports as the int surface type while ordinary float remains
// a separate surface value.
[type(1.0), type(1), type(big), type(big + 0.5)]
