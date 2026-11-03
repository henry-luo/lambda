// S11.4.12/S1.6: a non-structural cycle is rejected before emitting its value.
type U = U | int
let result = [U]
result
