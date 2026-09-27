// S11.1.2v3: island `!` complements a single-character set. A string of two
// characters is not one, so this is rejected instead of being dropped from
// the regex.
type bad_negation = \("<" (!"ab")* ">")
