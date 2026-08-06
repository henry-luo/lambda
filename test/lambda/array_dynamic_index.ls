// Tune12 regression: inferred function parameters must preserve the Item
// boundary when indexing a generic array with a statically typed element.

fn pick(x) { [196, 197, 64257][x] }
fn pick2(x) { [196, 197, 64257][x - 128] }

[pick(2), pick2(130)]
