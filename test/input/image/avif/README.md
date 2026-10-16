# AVIF decoder fixtures

These 32 by 24 pixel fixtures were encoded with libavif 1.3.0 and libaom
3.12.1 before enabling the decoder-only build. They use lossless YUV444,
the identity matrix, full range, quality 100, and encoder speed 10.

Pixel `(x, y)` has RGB `(7*x, 9*y, 4*(x+y))`. Alpha is 255, except in
`alpha.avif`, where it is `64 + (32*y+x) % 192`. `depth10.avif` stores the
same opaque pattern in 10-bit planes. The other fixtures use 8-bit planes.

`orientation_A_M.avif` sets `irot.angle=A`. For `M=1` or `M=2`, it also
sets `imir.axis=M-1`; `M=0` has no mirror property. The tests verify all
12 combinations against independently specified corner permutations.

Stored fixtures preserve decoding, alpha, high-bit-depth, thumbnail, and
orientation coverage without requiring an encoder in the test executable.
