// Bitwise operators accept integer lanes only; fractional values must not
// silently truncate through a raw native argument path.
[
    band(1.5, 1) is error,
    bnot(1.5) is error,
    shl(1, 1.5) is error,
    ushr(1.5, 1) is error
]
