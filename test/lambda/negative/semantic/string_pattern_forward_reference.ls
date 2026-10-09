// A pattern names only patterns defined before it: `B` comes later. The
// reference had compiled to nothing, so `A` silently matched only "".
type A = \(B+)
type B = \("\d")
