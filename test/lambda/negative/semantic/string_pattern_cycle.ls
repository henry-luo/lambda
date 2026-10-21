// S11.4.12: a top-level pattern may name a later one, but patterns compile to
// regular expressions, which cannot recurse: `A` reaches itself through `B`.
type A = \(B+)
type B = \(A "x")
