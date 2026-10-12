// GM13: `~` keeps the space away from a token's first character, so it marks
// a literal or a `:=` rule, never a `=` rule
grammar g {
    a = "x", ~b
    b = "y"
}
1
