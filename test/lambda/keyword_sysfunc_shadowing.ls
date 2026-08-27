// S12.3.7: user definitions shadow system functions — user-first, resolved
// statically, and scoped to this module/script only.
import m: .keyword_shadow_module

// a local definition wins over the builtin for every call site here.
fn len(xs) { -1 }
len("abc")

// the imported module shadows `sum`, but that shadow is NOT global: bare
// `sum` here is still the builtin. Shadowing is never ambient (unlike JS
// prototype mutation).
sum([1, 2, 3])

// the exported shadow is reachable through the explicit import.
m.sum([1, 2, 3])
