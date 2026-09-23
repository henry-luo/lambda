// J1 (S11.4.1v3): a literal crossing a null-accepting contract that the
// checker only deferred must reach the runtime check. The JIT read every
// literal as `null` here and bound `5` unchecked.
let w: int[] | null = 5
"bound: " ++ string(w)
