// S11.1.3: a range's bounds are single characters, in a pattern as in value
// position. The regex had taken the first byte of each bound.
type bad_range = \("ab" to "z")
