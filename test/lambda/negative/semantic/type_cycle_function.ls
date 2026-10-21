// S11.4.12: a function signature is not a structural position, so a function
// type that returns itself is a cycle.
type R = fn(x: int) R
