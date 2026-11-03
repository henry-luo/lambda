// S7.7.2v2: exported binding metadata includes its deferred validation outcome.
fn opaque(value) => value
pub let failed: int = opaque("bad")
pub let good: int = opaque(7)
null
