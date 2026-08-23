// IP2 fixtures [Type_Infer TI4]: registry rows now derive a precise success
// type instead of `any`. The polymorphic rows (math over complex/vectors,
// text over string|symbol) must keep answering by argument shape, so each
// case below pins both the narrowed and the still-open answer.
import math

// Real scalar transcendentals resolve to float.
let s = math.sqrt(2.0)
let c = math.cos(0.0)

// Rounding preserves the argument's numeric carrier.
let fl = floor(3.7)
let ce = ceil(3.2)

// Text transforms preserve the text family: string in, string out.
let up = upper("ab")
let tr = trim("  x  ")

// Order-preserving collection ops carry the argument's own type.
let sorted = sort([3, 1, 2])
let revd = reverse([1, 2, 3]);

[s, c, fl, ce, up, tr, sorted, revd]
