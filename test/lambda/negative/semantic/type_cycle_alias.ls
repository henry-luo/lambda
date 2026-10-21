// S11.4.12: two aliases that only name each other pass through no structural
// position, so matching them would never consume the value.
type A = B
type B = A
